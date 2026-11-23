# Minimal host counterpart spec — Hi5622V100 post-release bring-up service

**Lane:** host-replica. **Companion:** REPORT.md (evidence), skeleton/hi5622_host_replica.c
(compile-ready implementation). Target: our 6.18.60 transport module
(../wifi-transport-port/build). Host-only; no device contact in this document.

**Goal:** reproduce the vendor host driver's post-release service so the firmware boot chain
reaches its HCC task loop: accept the release, service the D2H mailbox, acknowledge every
exception-vector announce so the device returns from the handler, receive the two HCC
group-4 ready payloads, then keep serving rings/mailbox. No H2D mailbox traffic is required to
make that happen.

---

## 1. State machine

    S0 IDLE
      transport probe done, ETE rings allocated, firmware not yet released.
      actions: none to device.

    S1 RELEASED                      (transport pciet_cpu_release() → 0x5a5a @ 0x40000108)
      entry actions:
        do NOT pre-clear out[1]: the device may already have staged the first
        announce (the vendor only clears out[1] at probe, long before release);
        the first service tick consumes whatever is staged
        start poll timer (50 ms) and the D2H service loop
        start the ready-wait thread (or arm completions)
      exit: any D2H word observed, or a device HCC frame arrives.

    S2 GATE_SERVICE                  (loop while not READY)
      every service tick:
        bitmap = *out[1]; if bitmap == 0 -> also do the idle gate probe (below)
        ack/drain/re-arm (see section 3)
        for each set bit id <= 10: dispatch
        if bit 2 (id 2 = EXCP_PANIC):
            ack_gate()                     # *0x4000010c = 0x0000cece
            capture_vector_context()       # CA 0x400008 mask + 0x40000C..0x400044 frame
            log one line per event, increment counter
        continue
      idle gate probe (only while state < READY and no bitmap seen for > 1 tick):
        do NOT write 0xcece blindly; instead re-read *out[1] and, if the device is already
        spinning (out[1] bit2 set again), the normal path above handles it.
        [U] optional: a bounded retry (<= 3 writes, 100 ms apart) may be enabled for bench
        experiments; disabled by default because C1c-style hammering changes device state.

    S3 WAIT_PLAT_READY               (completion, 2000 ms = 200 jiffies at HZ=100)
      completer: HCC chip group 4 id 1 (device_plat_ready_msg_process semantics)
        * header chip id (hdr[1]>>4) must be 0, else log error and ignore
        * log "Device plat ready! chip id : <0>"; complete; flags |= BIT(0)
      timeout: log + dev_status digest + state S9

    S4 WAIT_DEVICE_READY             (completion, 20000 ms = 2000 jiffies at HZ=100)
      completer: HCC chip group 4 id 2 (host_ready_msg_process semantics)
        * header chip id must be 0
        * total = u16(hdr[4..5]); payload = total - 12
        * vendor validation: (total - 13) <= 98, i.e. total <= 111; payload <= 99;
          copy payload into read_msg[chip][core] (100-byte stride)
        * ready_map |= BIT(core); complete when ready_map == 1
        * log "DEVICE READY"
      timeout: log + dev_status digest + state S9

    S5 READY
      * service D2H mailbox forever (section 3)
      * drain DR rings on id 3 / 6 / 7 and after any SR send
      * restart the 10 s heartbeat on group-4 id 4 when armed
      * H2D allowed: id 3 announces only (plus PM id 1 only if the PM layer is enabled)
      * H2D id 5 (bugon) is NEVER sent in this state (guard flag)

    S9 FAILED
      log once; keep servicing the mailbox (the device may still recover or report a panic);
      do not send H2D id 5.

Transitions: S0 → S1 on release; S1 → S2 immediately; S2 → S3 when the wait thread arms;
S3 → S4 on g4 id1; S4 → S5 on g4 id2; any timeout → S9. The gate service continues in S3/S4
(and conceptually in S5) so late vector announces are still released.

---

## 2. H2D frames (the complete set needed for bring-up)

### 2.1 Non-mailbox writes

| when | write | value | notes |
| --- | --- | --- | --- |
| release | CA 0x40000108 | 0x00005a5a (4 B) | transport pciet_cpu_release(); vendor: bal_write quit descriptor |
| per vector announce | CA 0x4000010c | 0x0000cece | vendor writer [U]; our only known gate release; write once per observed out[1] bit2 |
| per D2H word | out[3] 0x40101438 | 1 | ack |
| per D2H word | out[1] 0x40039014 | 0 | drain (P2) |
| per D2H word | out[4] 0x40101414 | 1 | re-arm |

**Forbidden writes (hard rules + synthesis):**
- out[5] CA 0x400392f0: never.
- RC misc window 0x10161000: never (read or write).
- No SoC CRG/clock/ACP-gate writes are needed post-release (vendor does none at this stage).

### 2.2 Mailbox H2D announce (id 3)

Used only after READY when an HCC frame is committed to an SR ring:

    1. wait until *out[0] (0x40039010) reads 0; bounded ~0xFFFE polls; on timeout return -EBUSY
       (vendor behaviour; our transport currently polls out[2] — fix to out[0])
    2. pending |= BIT(3); if in-flight == 0:
           in-flight = pending
           *out[0] = pending
           *out[2] = *out[2] | 1          (RMW, CA 0x400392d4)
    3. return 0

Ids allowed on this path: 1 (PM sleep request, only with PM enabled), 3 (ETE announce), 5
(guard-gated: never on a normal path). Never 2/4/8/9/10.

### 2.3 HCC frame (SR path, id 3 announce)

    12-byte header (LE):
      [0]  group       = HCC group number (low nibble; g4 = boot/reserved)
      [1]  bid         = (chip/core selector) << 4        (vendor: hdr[1]>>4 = chip id)
      [2]  vap id
      [3]  chip id
      [4..5] u16 total length = payload + 12
      [6..7] u16 message id
      [8]  field C
      [9]  dst core
      [10..11] 0x5a5a  transport tag
      [12..] payload (<= 0x674 - 12 vendor max)
    then node 8 B: {buffer device VA, (len<<16) | 0x6d2b}; advance SR producer;
    then mailbox H2D id 3.

For the boot handshake the host sends **nothing**: the two group-4 messages are device→host.

---

## 3. D2H service loop (exact order per observed word)

    1. ack:   *out[3] = 1
    2. read:  bitmap = *out[1]
    3. drain: *out[1] = 0
    4. re-arm:*out[4] = 1
    5. dispatch, lowest set bit first, ids 0..10; stop at first id > 10
       id 1 -> stub: check hdr chip id == 0, log
       id 2 -> EXCP_PANIC: exceptions work item semantics
               * ack_gate() immediately (write 0xcece)  [our counterpart for the vendor's
                 missing exit path; see REPORT section 4.3/9]
               * capture CA 0x400008 mask and CA 0x40000C..0x400044 frame (read-only)
               * do NOT send H2D id 5 (vendor does after its full dump; we want the device
                 alive for the next iteration)
       id 3 -> ETE transfer done: drain DR channels (bitmask 0x1f in the vendor call)
       id 4 -> PM: device disallows sleep (PM layer only)
       id 5 -> PM: device wake-up ok (PM layer only)
       id 6 -> wake service thread (drain DR, release completed TX buffers)
       id 7 -> same as 6
       else -> count + one-time log

Polled-mode deviation (documented): our RC cannot demux INTx yet, so steps 1-4 run in a
50 ms poll thread. In IRQ mode (future) the vendor order ack → read is observed; in polled
mode the read happens first, then ack/drain/re-arm. Neither changes the device-visible register
sequence: ack=1, out[1]=0, re-arm=1, all before any handler runs.

Re-entrancy: service runs under one lock; the gate ack is issued from inside the id2 dispatch,
i.e. after out[1] was drained and re-armed — same order a vendor IRQ would produce.

---

## 4. Timings

| constant | value | source |
| --- | --- | --- |
| plat-ready wait | 2000 ms (200 jiffies @HZ=100) | vendor multi_chip_loading; msecs_to_jiffies(2000) |
| device-ready wait | 20000 ms (2000 jiffies) | same |
| D2H poll | 50 ms | transport bench lesson; device posts READY ~3 s after release |
| heartbeat | 10000 ms one-shot, restarted by g4 id4 | vendor ktime literal 0x2540BE400 |
| PM sleep-ack wait | 500 ms (50 jiffies) | vendor wlan_pm_sleep_work |
| H2D out[0] drain | ~0xFFFE reads, no delay (vendor) | pcie_msg_wait_for_clr |
| SR/DR depth | 32 channels; index encoding low 10 bits + phase 0x400 | vendor channel table |

---

## 5. Acceptance criteria (bench, attended)

Success:
1. after release + gate service, CA 0x400008 (vector mask) stops growing;
2. HCC chip-group-4 id 1 arrives within 2 s → "Device plat ready!";
3. chip-group-4 id 2 arrives within 20 s → "DEVICE READY", read_msg filled;
4. then id 4 heartbeats / later HCC frames appear; out[1] no longer constantly re-announces 2.

Failure:
1. vector mask keeps growing and out[1] bit2 re-announces repeatedly → capture the frames and
   stop (the gate release alone did not fix the fault);
2. no group-4 frame inside 20 s but mask stable → the device is past the handler but not in the
   task loop; capture ETE/DR state and the 11 dev-status words.

Stop conditions: never write out[5]; never read the misc window; never send H2D id 5.

---

## 6. Unknowns carried into the implementation

| item | status | effect on implementation |
| --- | --- | --- |
| 0xcece peer writer in the vendor stack | [U] | we write it ourselves; one write per observed vector announce |
| out[5] = 8 semantics | [U] | never written by us |
| vendor DR buffer size / free-buffer count at first publish | [U] | skeleton uses transport's 32 x 2048 B geometry |
| intent/periodicity of early vector entries | [U] | capture-only; no blind retry loop |
| PM hcc timer params (100/9) | [I] | PM disabled in the skeleton by default |
