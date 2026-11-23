# Vendor host driver, post-release behaviour — and the minimal host counterpart we are missing

**Lane:** host-replica (host-only). **Written:** 2026-11-23.
**Scope:** static reversal only. No bench contact, no image/OpenWrt build, no vendor binary,
object code, source text or firmware bytes are reproduced here — only addresses, numeric ids,
log strings and original analysis/code.
**Inputs (local evidence, path reference only, never committed):**

- vendor 2.5.24 modules: /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_{plat,wifi}.ko
  (plat 364,660 B / wifi 3,564,728 B, ELF32 ARM, not stripped, vermagic 5.10.201)
- vendor 2.4.15 modules: /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_{plat,wifi}.ko
- our transport sources: ../wifi-transport-port/build/{hi5622_pciet.h,pciet_main.c,pciet_ete.c}
- prior lanes: ../wifi-boot-kick/REPORT.md, ../wifi-hcc-re/REPORT.md,
  ../wifi-fw-ready-gate/REPORT.md, ../wifi-driver-re/REPORT.md, ../wifi-firmware-re/REPORT.md

**Evidence markers** follow the project convention: **[V]** re-verified by this lane from a named
local artifact (module + symbol/address), **[V*]** device-measured by an earlier attended lane
and quoted from the named report, **[I]** inference, **[U]** unknown / not yet pinned.

**Tools:** tools/annotate.py (relocation-aware ELF annotator for ARM .ko: every MOVW/MOVT/
ABS32/CALL relocation resolved to string/data/target), tools/census.py (call-site census),
tools/tables.py (data/rodata table dumper with reloc resolution), tools/regen.sh.
All tables below are reproducible with those tools (section 12).

---

## 0. TL;DR

1. **The vendor host never writes the 0xcece gate.** An independent scan of all 60+ vendor
   .ko in both rootfs trees for the immediate 0xcece, the CA 0x4000010c and the value word
   0xcece finds **0 hits** [V]. The only vendor file with CA bytes 0x4000010c is hi_kmac.ko
   (a SoC MAC driver; unrelated register, not the endpoint path) [V]. The gate's peer writer is
   still [U]; our transport's explicit 0xcece write [V*] is the only known trigger.
2. **Post-release H2D surface is tiny.** After the CPU release the vendor host does, in order:
   (a) enable the host IRQ (bal_irq_enable -> pcie_irq_enable -> enable_irq) [V];
   (b) serve the D2H mailbox with the fixed ack/drain/re-arm order [V];
   (c) send mailbox **id 3** (ETE SR announce) whenever an HCC frame is committed to an SR ring
   [V]; (d) send **id 1** only for a PM sleep request [V]; (e) send **id 5** only on crash/ETE-RX
   failure paths [V]. Nothing is sent between (a) and the first device HCC message.
3. **The CPU release itself is a 4-byte data write**, not a mailbox message: the last action of
   firmware_download() is a bal_write(chip, 0x40000108, &g_quit_cmd.value, 4) where
   g_quit_cmd = {0x40000108, &0x5a5a, 4} lives at .data+0x26a4 [V]. Our transport's
   pciet_cpu_release() reproduces it exactly [V*].
4. **The loaded vendor D2H handler table registers ids {1,2,3,4,5,6,7}.** Earlier reports found
   only {1,3,6,7} (from pcie_msg_init); ids 2, 4, 5 are registered later by
   exception_mgmt_res_init (id 2 = EXCP_PANIC) and wlan_pm_init (id 4 = sleep-disallow,
   id 5 = wake-device-ok) during the first wlan_power_on() [V]. **Correction to
   wifi-hcc-re/REPORT.md section 2.3/5.3.**
5. **The bench "DEVICE READY (bitmap 0x4)" is mailbox id 2 = EXCP_PANIC, not the ready
   handshake.** The firmware routine that writes out[1]=4 (file 0x86E14) is the non-reset
   exception/startup vector handler decoded by the earlier fw-ready-gate lane; it zeroes CA
   0x4000010C/0x40000108, announces, then spins for host 0xcece [V from that lane's decode,
   re-anchored here]. The true handshake is HCC chip-group-4 id 1 then id 2, consumed by
   g_multi_chip_init_res's 200/2000-jiffy completions [V].
6. **Minimal missing host counterpart = event-driven gate service + full D2H/HCC receive path.**
   A single-shot 0xcece is consumed by the first vector entry; every new entry re-zeros the
   gate and re-announces out[1]=4. The host must ack the gate **per announced event** while
   waiting for group-4 HCC id 1/2 (SPEC.md). On a cold boot this is the most likely reason the
   device parks in the vector handler while the bench only ever saw bitmap 0x4.
7. **Ready waits (exact):** 200 jiffies = **2.0 s** then 2000 jiffies = **20.0 s** at HZ=100
   (our 6.18.60 and the 5.10.201 tree are both CONFIG_HZ_100 [V]); on timeout: log,
   dev_status_check() (11 status words), return OAL_FAIL 0x8b2d [V]. multi_chip_loading()
   is called from wifi.ko host_module_init before hal/hmac/wal init [V].
8. **2.4.15 vs 2.5.24: no behavioural difference in the boot/ready/D2H path** (identical g4
   table bytes, quit descriptor, registration ids, jiffy constants, log strings and payload
   handling) [V]. 2.4.15 just keeps symbols the 2.5.24 build inlined; 2.5.24's symbol set is a
   strict subset of 2.4.15's (1763 vs 1958 names) [V].

---

## 1. Method

Both vendor .ko files are **not stripped**, which makes the reversal address-exact instead of
pattern-guessing: plat.ko has 1763 named symbols (2.5.24) / 1958 (2.4.15), wifi.ko 16,570 /
17,059. The work was done on full objdump -dr output of both versions and both modules with a
relocation-aware annotator: every ARM MOVW/MOVT/ABS32/CALL relocation is resolved through the
ELF .rel.* + symtab to the exact target section/offset, then strings and table words are read
from the target section. That is how the tables in this report (g4 boot table, quit descriptor,
memory-info table, status table, channel table) were recovered with byte precision.

Call-site censuses (bal_msg_register, pcie_msg_register, pcie_msg_send, hcc_msg_tx,
wait_for_completion_timeout, ...) were produced mechanically over the whole text section, so the
H2D/D2H vocabularies below are complete for both modules, not sampled.

---

## 2. Vendor post-release sequence (authoritative order)

The boot is driven from **wifi.ko**: init_module -> host_module_init (0x3e164, wifi.ko) [V]:

    host_module_init:
      oal_net_dev_res_init()
      oam_wifi_init()                         -> on error: unload/return
      multi_chip_loading()                     (imported from plat.ko)   <-- power-on + fw + waits
      hal_main_init()
      hmac_main_init()
      wal_main_init()

multi_chip_loading() (plat 0xf2ac) [V]:

    g_multi_chip_init_res.flags = 0; .ready_map = 0;
    init both completions
    wlan_pm_open(0):
        if (pm->init_done == 0) {
            plat_init_bal_hcc_excp(0):              # BEFORE any firmware download
                exception_mgmt_res_init(0)          # excp ctx + heartbeat init + bal msg id 2
                hcc_msg_register_tab_chip(4, .data+0x2660, 5)  # boot HCC table (g4)
                wlan_pm_init(0)                     # wlan_pm_wq, IRQ 82, bal msg id 4/5, PM timer
            pm->init_done = 1;
        }
        hwifi_rf_cali_file_load(0)
        firmware_download_function(0):
            get_chip_type/path; firmware_download(ctx):
                version check
                alloc 0x80000 bounce; for each 0x80000 chunk:
                    firmware_kernel_read(chunk) -> bal_write(chip, FW_TARGET_CA+off, buf, len)
                "quit send": bal_write(chip, 0x40000108, &g_quit_cmd.value, 4)  # 0x5a5a release
        ktime stats + printk "power on, count..., current time..., max time..."
        bal_irq_enable(0) -> pcie_irq_enable(0) -> oal_enable_pcie_irq -> enable_irq()
    wait_for_completion_timeout(&plat_ready,  200 jiffies)   # 2.0 s
        success: flags |= 1; printk "wlan_pm_open SUCC chip_id[0]"; dev_status_check(0)
        timeout: printk "multi_chip_loading::plat_ready timeout[0x%x]"; dev_status_check; return 0x8b2d
    wait_for_completion_timeout(&host_ready, 2000 jiffies)   # 20.0 s
        success: dev_status_check(0); printk "multi_chip_loading SUCC!"
        timeout: printk "device_ready timeout plat_ready[0x%x] device_ready[0x%x]"; dev_status_check; fail

Notes:
- The completion structure is g_multi_chip_init_res (44 B in 2.4.15, named; anonymous
  .bss+0x3c58 in 2.5.24): {u32 flags; u32 ready_map; struct completion host_ready;
  struct completion plat_ready;} — flags bit0 = plat ready [V]. 2.4.15 additionally exports
  get_plat_ready_flag() / get_device_ready_flag() getters of the two words [V].
- The completers are the HCC group-4 handlers (section 5): id 1 completes plat_ready (only if
  header chip id 0); id 2 copies its payload into g_dmac_to_hmac_read_msg, sets the per-core
  ready bit and completes host_ready when ready_map == 1 [V].
- The **first host IRQ enable happens after firmware download** — i.e. after release [V].
- The firmware transfer is a pure memory copy into CA 0x01240000 (BAR0+0x6f8000) in 0x80000
  chunks via bal_write/pcie_write (memcpy_s into the inbound-mapped VA). No ring, no per-chunk
  header [V]. This matches our transport's pciet_fw_upload() [V*].

### 2.1 Complete post-release host->device write list

| # | when | target | value | path | evidence |
| --- | --- | --- | --- | --- | --- |
| 1 | end of firmware_download() ("quit send") | CA 0x40000108 | 0x00005a5a (4 B) | bal_write -> pcie_write -> memcpy_s to mapped VA | [V] .data+0x26a4 descriptor; [V*] our B1.4 |
| 2 | after download | host PCI IRQ line | enable_irq() for each pci dev | bal_irq_enable(0) -> pcie_irq_enable(0) -> oal_enable_pcie_irq | [V] wlan_power_on tail |
| 3 | on every D2H mailbox word | out[3] CA 0x40101438 | 1 (ack) | pcie_msg_handle | [V] |
| 4 | same | out[1] CA 0x40039014 | 0 (drain, device precondition P2) | pcie_msg_handle | [V] |
| 5 | same | out[4] CA 0x40101414 | 1 (re-arm) | pcie_msg_handle | [V] |
| 6 | on H2D send (data) | out[0] CA 0x40039010 | pending bitmap | pcie_msg_send / pcie_msg_send_irq | [V] |
| 7 | on H2D send (doorbell) | out[2] CA 0x400392d4 | old OR 1 (RMW) | same | [V] |
| 8 | deferred H2D variant | out[5] CA 0x400392f0 | 8 | pcie_msg_send_irq prologue | [V]; **project hard rule: our code must not write it** (our transport keeps the write disabled) |
| 9 | gate ack | CA 0x4000010c | 0x0000cece | **no vendor writer [V]; ours writes it [V*]** | [U] peer |

There is **no** post-release setup write to the ETE block, the ACP gate or any SoC CRG in the
vendor path: ETE rings are armed at PCI probe (before upload), and pcie_msg_init runs at probe
too [V]. Our transport's post-release arming of SR/DR is a bench-driven equivalently-valid path
[V*] (and needed because our RC driver has no INTx yet).

---

## 3. H2D message catalogue (mailbox ids and HCC frames)

### 3.1 Mailbox vocabulary (11-bit bitmaps, ids 0..10)

pcie_msg_register rejects id > 10 or NULL handler [V]; pcie_msg_send accepts only id <= 9
and chip 0 [V]. Vendor sends only these three ids after release [V]:

| id | sender | when | retries/timeouts | evidence |
| --- | --- | --- | --- | --- |
| **1** | wlan_pm_sleep_work -> bal_msg_send(0,1) | host-initiated PM sleep request | waits 50 jiffies (0.5 s) for the sleep-ack completion, then restores/aborts | [V] |
| **3** | shuangta_ete_sr_dscr_fill -> pcie_msg_send(chip,3) | after EVERY SR node commit (a batch of HCC messages) | coalesced by pending/in-flight; no retry loop; announce may return 0x8b2d only on bad args | [V] |
| **5** | exception_handler tail ("chip%d send msg_try_bugon!!!"), pcie_ete_rcv_buff_check failure branch, hcc_queue_msg_zero_len_debug | crash/abort/failure only | none | [V]; device-side H2D id5 handler is the firmware BUG() trap — **never post 5 on a normal path** |

H2D id 1 goes to the firmware's "post global msg object" handler (device file 0x510 [V from
wifi-hcc-re section 3.5]); ids 2/4/8/9/10 are never sent by the vendor [V].

### 3.2 H2D send primitive (exact, corrections vs our transport)

pcie_msg_send(chip, id) (plat 0x160f4) [V]:

    reject if id > 9 or chip != 0                       -> 0x8b2d
    spin_lock(chip+0x50)
      pending = chip->pending (chip+0x48)
      if !(pending & (1<<id)):
          pending |= 1<<id; chip->pending = pending
          if chip->inflight (chip+0x44) == 0:
              chip->inflight = pending
              *out[0] = pending                        # CA 0x40039010
              *out[2] = *out[2] | 1                    # CA 0x400392d4, read-modify-write
    spin_unlock
    return id

pcie_msg_send_irq(chip) (deferred variant, installed as each pci device's +0x60 callback)
(plat 0x174a8) [V]:

    *out[5] = 8                                        # CA 0x400392f0 — do not copy (hard rule)
    pcie_msg_wait_for_clr(&chip->out[0]):
         spin until *out[0] == 0, at most 0xFFFE reads, NO delay -> 0x8b2d on timeout
    spin_lock; chip->inflight = chip->pending; if pending: *out[0]=pending; pending=0; *out[2]|=1
    else pcie_wkup_thread(chip)

> **Correction for our transport (actionable):** pciet_msg_wait_for_clr() currently polls
> **out[2]** (CA 0x400392d4). The vendor waits on **out[0]** (CA 0x40039010) — the pending mask
> the device clears when it consumes it. The host's own doorbell write is the *last* step, so
> waiting on it cannot work in general. SPEC.md/skeleton implement the vendor order
> (wait out[0]==0 -> out[0]=mask -> out[2]|=1).

### 3.3 HCC frame on the wire (SR node + 12-byte header)

Our SR send path reproduces the vendor's node/header contract; re-confirmed anchors [V]:

- SR node 8 B: word0 = buffer device VA, word1 = (len<<16) | 0x6d2b
  (shuangta_ete_sr_dscr_fill 0x17858: sets bit14+bit13+d2b when "present").
- buffer = 12-byte HCC header + payload; header (little-endian):
  [0] type/group (low nibble = HCC group), [1] bid (high nibble = chip/core selector),
  [2] field A (vap), [3] field B (chip id), [4..5] u16 total length = payload+12,
  [6..7] u16 message id, [8] field C, [9] dst core, [10..11] 0x5a5a transport tag,
  payload at [12].
  Evidence: builders wal_alloc_cfg_event (group 2, id 0, hdr[12] = 4-byte value,
  length = req_len+4) and the multiap metric builder (group 0 low nibble, id 0x2d, hdr[2]=vap,
  hdr[3]=chip, hdr[8]=0) [V]; hcc_msg_alloc max 0x674 and len field = size+12 [V].
- SR commit then pcie_msg_send(chip,3).

---

## 4. D2H service model

### 4.1 The six message registers

shuangta_pcie_msg_reg_map() (plat 0x1b1a0) maps, **in this order**, the six CAs into the
chip/msg context (chip+0x2c..+0x40) [V]:

| out | CA | BAR0 | host role |
| --- | --- | --- | --- |
| out[0] | 0x40039010 | +0x3f1010 | H2D pending mask (host writes; device consumes/clears) |
| out[1] | 0x40039014 | +0x3f1014 | D2H pending mask (device writes; host ack/drain) |
| out[2] | 0x400392d4 | +0x3f12d4 | H2D doorbell (host ORs bit0; device writes 8) |
| out[3] | 0x40101438 | +0x4b9438 | D2H ack (host writes 1) |
| out[4] | 0x40101414 | +0x4b9414 | D2H re-arm (host writes 1) |
| out[5] | 0x400392f0 | +0x3f12f0 | H2D ack / "copy A" (vendor writes 8 in the deferred-send path) — **project hard rule: never write from host** |

Probe-time arm (pcie_msg_init 0xb6e4): map the six words via the bus ops, then
*out[0]=0; *out[1]=0; chip->inflight=0; chip->pending=0; allocate and zero the 11x8 B handler
table (chip+0x4c); per PCI device install {+0x60 = pcie_msg_send_irq, +0x64 = chip,
+0x68 = pcie_msg_handle, +0x6c = &chip->out[0]} [V]. No other register write at probe.

### 4.2 D2H receive algorithm — pcie_msg_handle(&chip->out[0]) (0x171f8) [V]

    *out[3] = 1                 # ack            (CA 0x40101438)
    bitmap  = *out[1]           # read           (CA 0x40039014)
    *out[1] = 0                 # drain (P2, mandatory before device's next publish)
    *out[4] = 1                 # re-arm         (CA 0x40101414)
    while bitmap:
        id = ctz(bitmap)        # lowest set bit
        if id > 10: break
        fn = table[id].fn; arg = table[id].arg          # 8-byte slots, table at chip+0x4c
        if fn: fn(arg)
        clear bit id

pcie_msg_register(handle, id, fn, arg) (0x15fbc): rejects id > 10 or fn == NULL
(OAL_FAIL 0x8b2d); writes {fn,arg} at table[id*8]; handle==NULL selects the global
per-chip-0 context [V].

### 4.3 Registered D2H ids in the fully-loaded driver [V]

| id | handler | registered by | semantics / action |
| ---: | --- | --- | --- |
| 1 | pcie_dev_ready_msg_handle 0x8784 | pcie_msg_init | empty bx lr stub (platform-up nibble; no action) |
| **2** | exception_panic_msg_process (0x136c, .text.unlikely) | exception_mgmt_res_init via bal_msg_register(0,2,...) | "Enter host_plat_panic_msg_process!!!"; exception_submit(1<<chip, EXCP_PANIC=0) -> exceptions workqueue |
| 3 | pcie_ete_transfer_done_handle 0x8730 | pcie_msg_init | pcie_ete_d2h_isr_handle(chip,0,0x1f) -> drain DR channels bitmask 0x1f |
| **4** | wlan_pm_slp_disallow_callback 0xe700 | wlan_pm_init via bal_msg_register(0,4,...) | device refuses sleep; PM state only |
| **5** | wlan_pm_wkup_dev_ok_callback 0xe20 (.text.unlikely) | wlan_pm_init | device wake-up is OK; completes PM wake path |
| 6, 7 | pcie_trigger_ete_sending_handle 0x15efc | pcie_msg_init | pcie_wkup_thread(chip) — wake the TX/service kthread |
| 0, 8, 9, 10 | — | — | dropped ("no handler registered") |

**Correction:** wifi-hcc-re/REPORT.md section 2.3/5.3 lists only {1,3,6,7}; the two later
registration sites above were missed. This matters on the bench: the observed out[1]=0x4 stays
a *panic* announcement even in the fully-loaded driver, and the correct host action for it is
the exception path (here: gate ack + context capture), not "ready".

### 4.4 ISR / thread model (vendor; ours is polled)

- **INTx:** oal_pcie_probe_irq_init requests a threaded IRQ named "hisi_pci_intx"
  (request_threaded_irq(irq, NULL, oal_pcie_intx_isr)) at probe [V].
  oal_pcie_intx_isr -> oal_pcie_transfer_done: read/clear the SoC PCIe controller's status
  word (dev->[0]->[0xc]+8, write-1-to-clear via +4; 0xFFFFFFFF = no device), then
  pcie_ete_h2d_isr_handle, pcie_ete_d2h_isr_handle(chip, 0, status>>16), and
  pcie_intr_handle(chip) which reads the chip cause register (chip+0x2ec) and dispatches
  mask 0x3d8 bits through callbacks at dev+0x48+8*idx [V].
- **D2H service kthread:** pcie_thread_init creates pcie_process_thread (kthread) bound to
  the chip; it waits on a waitqueue (chip+0x18) for a one-word flag at chip+0x28; the flag
  is set by pcie_wkup_thread(chip) (*[chip+0x28]=1; __wake_up(chip+0x18)). There is **no
  poll interval and no timeout** in this thread — it is purely event/wake driven [V].
- **D2H GPIO IRQ:** wlan_pm_init additionally requests threaded IRQ 82 ("d2h_gpio_intr")
  -> wlan_pm_d2h_gpio_irq_callback for the PM wake path [V].
- **Our 6.18 RC driver has no INTx** (luofu host driver msi_init returns 0; misc window never
  read) [V*], so our transport polls out[1] every poll_ms (500 ms default, 50 ms recommended
  after start). This is a valid substitute **provided the gate ack is driven by the observed
  out[1] bit2 event** (SPEC.md).

---

## 5. HCC init / registration sequence

### 5.1 plat.ko module init (before any PCI probe) [V]

init_module 0x1a75c: plat_res_init, oal_main_init, oam_main_init, sdt_drv_main_init,
low_power_init, plat_hcc_init. plat_hcc_init 0x1a690: plat_custom_init, plat_main_init,
pcie_init_static_res, plat_debug_sysfs_init, bal_init, hcc_init, plat_exception_init.
PCI probe pcie_main_init (delayed until the endpoint enumerates, .text.unlikely 0x704):
pcie_init_default, pcie_bus_getnum, pci_register_driver hook, get_pcie_res, pci_dev_res_init,
oal_pcie_enable_remap, pcie_ete_init, pcie_comm_init; pcie_comm_init =
pcie_thread_init + pcie_msg_init [V].

### 5.2 The boot HCC table (chip group 4) [V]

Registered by plat_init_bal_hcc_excp() (called from wlan_power_on() **before** the
firmware download), hcc_msg_register_tab_chip(4, .data+0x2660, 5):

.data+0x2660, 5 records of 12 B {u16 id; u16 pad; u32 fn; u32 aux}:

| id | handler | aux | semantics |
| ---: | --- | ---: | --- |
| 0 | oam_rx_post_action_function 0xdabc | 3 | device log blob -> SDT/netlink |
| **1** | device_plat_ready_msg_process 0xeb78 | 3 | checks hdr chip id == 0, prints "Device plat ready! chip id : %d", **completes the 200-jiffy wait** |
| **2** | host_ready_msg_process 0xebd8 | 3 | validates l_msg_len (total) against sizeof g_dmac_to_hmac_read_msg = 100, copies total-12 bytes into g_dmac_to_hmac_read_msg[chip][core], sets ready bit, **completes the 2000-jiffy wait when ready_map==1** |
| 3 | exception_info_msg_process 0x18c0 (.text.unlikely) | 3 | copies 0x30-byte exception info body into the exception ctx |
| 4 | heartbeat_msg_process 0x10214 | 3 | restarts the 10 s heartbeat hrtimer (no-op if heartbeat not armed) |

### 5.3 Registration API and dispatch [V]

- hcc_msg_register_tab_chip(group, table, count) -> ..._customise(type 0, ...);
  hcc_msg_register_tab_core(group, ...) -> customise types 1 and 2 (both core maps).
  Record format is the 12-byte {u16 id; u16 pad; u32 func; u32 aux} above [V].
- hcc_msg_process(reg_tab, msg) (0x1204c): header ptr at msg+0x118; group = hdr[0]&0xf;
  map = *(reg_tab+4), n_groups = *(map+0x10); rec = *(reg_tab+0x10) + group*12;
  id = u16 hdr[+6]; fn = *(u32*)(*(u32*)(rec+4) + id*16 + 4); call fn(msg) [V].
- wifi.ko registers its own tables into the same plat HCC registry during
  hdpp_main_init (chip g3, core g0), hmac_main_init (chip g1/g2/g3), wal_main_init
  (chip g2) — catalogues already recorded in wifi-hcc-re section 5.5 [V*] and re-checked here
  for the registration call sites only (the 2.5.24 call list is unchanged) [V].
- HCC state byte: only writers are plat hcc_start-equivalent (<-1 at init) and wifi.ko
  hmac_main_init (<-4) / wal_main_init (<-5) [V from call sites of hcc_set_state].

---

## 6. Host-side objects the firmware expects (and how the vendor allocates/links them)

Everything below is host RAM or endpoint-visible registers; the device never owns these
allocations (device-side "pointers" in the protocol are values the *host* publishes).

### 6.1 ETE rings and buffers [V, re-anchored]

- 7 channels at block CA 0x4003a000, config table .rodata+0x101c (7 x 12 B):
  {block_off, depth/ctrl word 0x06800020, flags} with block offsets
  0x400/0x450/0x4a0 (SR0-2), 0x590/0x5e0/0x630/0x680 (DR0-3); depth byte (+4) = 0x20 = **32**;
  flags = 1 for SR0-2 and DR0 (control channels), 0 for DR1-3 [V].
- pcie_ete_rings_init: zeroes 3 SR contexts (stride 0x114) and the DR contexts (stride 0x6c),
  maps both ETE CAs (block 0x4003a000 and intr 0x40039508), then pcie_ete_init_src_ring /
  ..._dst_ring per channel [V].
- pcie_ete_intr_init: maps intr CA and clears mask bits: *intr &= 0xffe0f8f8; installs the
  h2d/d2h/error handler callbacks used by the ISR path [V].
- SR: host writes base(+0x10)=node-array device VA, depth-1(+0x14), producer(+0x18), ctrl(+0x08);
  node = 8 B {buffer device VA, (len<<16)|0x6d2b}; producer index encoding low 10 bits +
  phase bit 0x400 [V].
- DR: host writes base(+0x30)=node-array device VA, depth-1(+0x34), free-buffer index (+0x38);
  the device deposits payload+node and advances its service index (+0x3c) [V from earlier lanes,
  re-checked against pcie_ete_dr_reg_init]; host drains completed ranges
  (pcie_ete_get_rls_num arithmetic, mod-2*depth) and returns buffers by advancing +0x38 [V].
- Buffer geometry: vendor sizes come from its netbuf pool (per-message skbs); our transport uses
  32 x 2048 B per ring via dma_alloc_coherent [V*]. Exact vendor DR buffer length is **not**
  pinned by this lane [U]; the protocol only needs node len <= buffer size.

### 6.2 g_dmac_to_hmac_read_msg — the 100-byte per-chip D2H payload object

- In both versions the array lives at .bss+0x3be8; 2.4.15 names it
  g_dmac_to_hmac_read_msg (size **100**, symbol [V]); 2.5.24 keeps it anonymous and only
  references its size in the host_ready_msg_process log string [V].
- Filled by HCC g4 **id 2**: validates total len - 13 <= 98 (i.e. total <= 111), copies
  total-12 payload bytes into g_dmac_to_hmac_read_msg[chip][core] (stride 100), sets the core
  ready bit, completes the 2000-jiffy wait when the chip's ready map == 1 [V].
- Exported getter wlan_get_d2h_read_msg(chip) (0xe76c) returns the array (chip 0) or NULL +
  "chip_id[%d] >= max[%d] overflow"; wifi.ko **imports** it (undef symbol) [V] — i.e. the boot
  payload is consumed by the Wi-Fi driver after multi_chip_loading() returns.

### 6.3 Ready state: g_multi_chip_init_res

See section 2. 44 B in 2.4.15 ({flags, ready_map, host_ready completion, plat_ready
completion}); flags bit0 = plat ready; ready_map bit per core (bit0 only on this board). Getter
functions exist only in the 2.4.15 build [V].

### 6.4 Exception management resources (the panic path)

- Per-chip exception ctx at .bss+0x3cb8 + 0x108*chip [V]:
  chip id (+0), excp type (+4), work flags (+8/+0xc), exception-info CA (+0x58), CPU-info CA
  (+0x5c), dump-meminfo pointer (+0x7c), heartbeat-disabled flag (+0x80), hrtimer list linkage
  (+0x88/+0x8c), hrtimer (+0x90), exception_info copy destination (+0x98), work struct
  (+0xe8), recovery callbacks (+0xc8...+0xe4), workqueue pointer (+0x108), lock (+0x10c) [V].
- exception_mgmt_res_init(chip) (0x1051c): get_chip_type -> selects the memory-info table
  entry; heartbeat_init; bal_msg_register(chip, 2, exception_panic_msg_process, ctx);
  initialises the work/list fields. On failure returns 0xffff74d3 [V].
- Exception types (log string): **0 EXCP_PANIC, 1 EXCP_HEART, 2 EXCP_CHANNEL, 3 EXCP_WIFI** [V].
  exception_submit(chip_mask, type) picks chip 0 only ("wrong chip id" otherwise), sets the
  work type, queues the work item [V].
- **Vector context the firmware stages for the host (read-only, safe CAs):**
  halfword vector-seen mask CA **0x400008**; 17-word context frame CA **0x40000C..0x400044**
  = {r0-r12, sp, lr, lr_exc, spsr} [V from fw-ready-gate section 1.2/10.1]. Exception info body CA
  **0x410000** (SMAC DTCM), read via the inbound map [V].
- Dump memory ranges table (g_dump_meminfo, type 0, at .data+0x26b8, 3 x 16 B
  {ca, 0, size, name}) [V]:

  | ca | size | name |
  | ---: | ---: | --- |
  | 0x00040000 | 0x00180000 | excp_wram |
  | 0x01000000 | 0x000e0000 | excp_pktram |
  | 0x00410000 | 0x00008000 | excp_smac_dtcm |

- Vendor panic action: read the frame, print stacks/thread info, save the three ranges and the
  CPU info to files under /config/work/firmware/ and /log/wifi/, then **send H2D id 5**
  ("force bugon") [V]. For our bring-up we deliberately stop before that H2D id 5 (SPEC).
- **Safety note:** the vendor's EXCP_HEART path reads SoC PCIe registers at 0x10165100 /
  0x10161100 via ioremap (exception_pcie0_link_down_process, pcie0_linkdown.txt file) [V].
  The project hard rule says the RC misc window 0x10161000 is never read by our code — the
  replica must NOT reproduce that read; use the IRQ/gate status instead [V project rule].

### 6.5 Firmware download bookkeeping

- firmware_info (32 B per chip type, .bss+0x3c88): {chip, chip, core=0, chunks=1, path
  array ptr, quit-descriptor ptr, spec callback, stats...} [V].
- g_file_info (.data+0x269c, 8 B in 2.4.15): {path pointer, firmware target CA
  0x01240000} [V].
- g_quit_cmd (.data+0x26a4, 12 B): {ca 0x40000108, value source &0x5a5a, len 4} [V].

### 6.6 PM objects and heartbeat

- PM ctx per chip at .bss+0x3a40 + 0x1a8*chip; wlan_pm_init: alloc_workqueue("wlan_pm_wq"),
  request_threaded_irq(82, "d2h_gpio_intr", ...), bal msg ids 4/5, hcc timer for
  wlan_pm_try_to_sleep (parameters 100/9 — likely 100 ms poll / 9 retries [I]), mutexes and swait
  heads [V]. Sleep path sends H2D id 1 and waits 50 jiffies (0.5 s) [V].
- Heartbeat: one-shot 10 s hrtimer (ktime = 0x00000002540be400 ns = 10,000,000,000 ns) per
  chip; heartbeat_start/heartbeat_stop have **no caller in plat.ko and are imported only by
  plat_debug.ko/wifi_debug.ko** [V] -> the watchdog is armed by vendor debug tooling, not by
  the boot path. g4 id 4 restarts it when armed; expiry -> exception_submit(1, EXCP_HEART=1) [V].

---

## 7. The ready-message wait logic (200/2000 jiffies) and timeout behaviour

Exact code (identical in 2.4.15 and 2.5.24) [V]:

    wait_for_completion_timeout(&g_multi_chip_init_res.plat_ready,  200)   /* 2.0 s @HZ=100 */
    if (!ret) { printk("multi_chip_loading::plat_ready timeout[0x%x]", flags);
                dev_status_check(0); return 0x8b2d; }        /* OAL_FAIL */
    flags |= 1; printk("wlan_pm_open SUCC chip_id[%d]"); dev_status_check(0);

    wait_for_completion_timeout(&g_multi_chip_init_res.host_ready, 2000)   /* 20.0 s @HZ=100 */
    if (!ret) { printk("multi_chip_loading::device_ready timeout plat_ready[0x%x] device_ready[0x%x]",
                        flags, ready_map);
                dev_status_check(0); return 0x8b2d; }
    dev_status_check(0); printk("multi_chip_loading SUCC!");

- Both timeouts are **non-fatal to the kernel**: host_module_init sees 0x8b2d and returns
  failure (module load fails; the vendor modules are then typically not force-unloaded because
  refcounts/pci are held) [V/I].
- dev_status_check() always runs on success and on timeout: it converts 11 device CAs to host VA
  and prints "%s:: %s=0x%x!", with bitfields for dcoldo_efuse (bits 5-7) and dcoldo_vset
  (bits 6-9). Authoritative table (.rodata+0xcac) [V]:

  | name | CA |
  | --- | ---: |
  | lock_status | 0x40004208 |
  | pcie0_status | 0x40039224 |
  | pcie1_status | 0x40039a24 |
  | dcoldo_efuse | 0x400002d4 |
  | dcoldo_vset | 0x4000500c |
  | tcxo_pll_mux_sel | 0x40101230 |
  | tcxo_pll_status | 0x40101234 |
  | efuse_chip_id | 0x400002a8 |
  | pbank_code | 0x4000505c |
  | abank_code | 0x40005060 |
  | temp | 0x40004110 |

  (Our transport header's hi5622_dev_status[11] differs in two entries — it lists
  pcie0_latch and lacks pcie1_status; the table above is the decoded vendor one. Not
  material to the boot path, but worth aligning.)
- HZ: project 5.10.201 tree CONFIG_HZ_100=y, 6.18.60 tree also CONFIG_HZ_100=y [V], so
  jiffies map to 10 ms in both; the implementation should use msecs_to_jiffies(2000/20000)
  so the timeout survives an HZ change.

**Bench consequence:** on the bench only out[1] bit2 ever appeared; g4 id1/id2 never completed
either wait. With the corrected id map (section 4.3) that is consistent with the device sitting in
the vector handler (out[1]=4 = EXCP_PANIC announce) rather than in the task loop that emits g4
id1/2. The single-shot 0xcece from B1.5 releases at most one vector entry; the next entry
re-zeros the gate and re-announces.

---

## 8. 2.4.15 vs 2.5.24

| item | 2.4.15 | 2.5.24 | verdict |
| --- | --- | --- | --- |
| symbol count plat.ko | 1958 | 1763 | 2.5.24 is a strict subset [V] |
| g4 boot table .data+0x2660 | identical bytes (5 x {id,pad,fn,aux}) | identical | same [V] |
| quit descriptor .data+0x26a4 | {0x40000108, &0x5a5a, 4} | same | same [V] |
| D2H registrations | 1,2,3,4,5,6,7 | 1,2,3,4,5,6,7 | same [V] |
| 200/2000 jiffy waits + logs | same code/logs | same | same [V] |
| host_ready payload handling, 100-byte array | named g_dmac_to_hmac_read_msg | anonymous .bss+0x3be8 | same [V] |
| ready-flag getters | get_plat_ready_flag/get_device_ready_flag present | inlined/absent | cosmetic [V] |
| exception helpers | exception_preproc/exception_get_mod_res/exception_get_mgmt_res/get_excp_res/firmware_info/get_firmware_res present | inlined/absent | cosmetic [V] |
| helpers | h2d_write32/h2d_read32, g_quit_cmd, g_file_info, g_dump_meminfo, g_multi_chip_init_res named | anonymous | cosmetic [V] |
| bal extra ops | bal_get_cbs/bal_port_activate/deactivate/halt/bal_remove present | different/absent | out of boot path [V] |

No behavioural difference was found in the post-release/boot/D2H-service path between the two
shipped versions; the skeleton targets the common behaviour.

---

## 9. Minimal implementable host counterpart (summary; full spec in SPEC.md)

**What our transport already has [V*]:** iATU windows, honest firmware upload + verify, release
0x5a5a, gate 0xcece (one shot), SR0/DR0 rings, H2D announce primitive, bitmap read/drain, ids 1/2
handlers as "ready", poll thread.

**What is missing / must change (ranked):**

1. **Gate service is event-driven, not one-shot.** On every observed out[1] bit 2: run the D2H
   ack/drain/re-arm sequence **and** write 0xcece to CA 0x4000010c, capture the vector frame
   (CA 0x400008 mask + 0x40000C..0x400044), then continue waiting. This is the top candidate for
   the parked cold boot; it does not require any new H2D message.
2. **Full D2H id set** {1,2,3,4,5,6,7} with vendor semantics (id2 = exception, id3 = DR drain,
   id4/5 = PM, id6/7 = wake, id1 = stub with hdr chip-id check).
3. **Full ack/drain/re-arm order** before dispatch (our scan drains out[1] only; ack out[3] and
   re-arm out[4] are missing today).
4. **HCC receive path**: parse the 12-byte header, dispatch chip group 4 through the 5-entry boot
   table, copy the id2 payload into a 100-byte per-core record, complete the two waits
   (2 s / 20 s).
5. **ETE**: arm SR0-2 and DR0-3 (not just SR0/DR0), DR drain driven by D2H id3 (channel mask
   0x1f) and by the wake ids 6/7; return DR buffers by advancing the DR free index.
6. **HCC transmit**: header + 0x5a5a tag + SR node + id3 announce; H2D id1 (PM) and id5
   (bugon) behind explicit guards, never on the boot path.
7. **Exception context capture** (read-only CAs) so a vector entry produces a report instead of
   only a parked device; do not read the RC misc window and do not send H2D id5 during bring-up.
8. **Heartbeat**: 10 s one-shot hrtimer restarted by g4 id4 when armed (debug tooling arms it in
   the vendor; make it optional in ours).

**Strictly not needed for the boot handshake:** any H2D mailbox message, any write to out[5], any
SoC CRG/clock enable, any exception-memory *write*, any reading of the misc window.

**Status of the skeleton:** it compiles clean against the project's 6.18.60 kernel tree as an
additional object of the transport module (CC/LD/MODPOST all pass; see skeleton/BUILD-NOTES.md).
It is not loaded on the bench by this lane.

---

## 10. Unknowns (honest limits)

- **[U] Who writes 0xcece in the vendor world.** Not in any vendor module (both versions, all
  60+ modules); not in the firmware image logic paths we decoded (the firmware only *waits*).
  Candidates: SoC boot chain built-in driver, u-boot, or a vendor userspace helper writing a
  mapped page. Our transport writes it explicitly and the device reacts [V*].
- **[U] out[5] semantics.** Vendor deferred-send writes 8 to 0x400392f0; the device writes 1
  after consuming an H2D message [V*, wifi-hcc-re section 3.5]. Our hard rule forbids host
  writes; the bench H2D smoke completed without it [V*], so the 8 is not required for id3
  delivery.
- **[U] Exact vendor DR buffer size / initial free-buffer publication.** Our 32x2048 B model is
  bench-valid for SR/DR0 smoke only.
- **[U] Intent of the exception vector entries at boot.** Whether the firmware intentionally
  takes these exceptions during normal boot (and returns after 0xcece) or only after a fault is
  not decodable statically; the parked cold-boot state (two/three vector bits set) suggests a
  real fault that the handler catches and reports. Gate ack + frame capture is the instrument.
- **[U] The 1<<vector mask polarity / vector numbering vs CA 0x400008** (previous lane decoded
  the write as "OR (1<<core)" then corrected to "1<<vector"; read the mask on the next bench
  session with the new gate loop to settle it).
- **[I] PM timer parameters** 100/9 (ms / retries) are inferred from call arguments.

---

## 11. Next bench step (attended, single action; not performed by this lane)

1. Load the current transport + the skeleton's gate service (or write 0xcece from the poll loop
   on every out[1] bit2).
2. After release + first announce: poll out[1] every 50 ms. On bit2: ack (out[3]=1), drain
   (out[1]=0), re-arm (out[4]=1), **write 0xcece**, and log CA 0x400008 (mask) + the 17-word
   frame CA 0x40000C..0x400044.
3. Success signature: g4 id 1 within 2 s ("Device plat ready! chip id : 0"), then g4 id 2
   ("DEVICE READY") within 20 s, then subsequent HCC frames / heartbeat id4 appear; the exception
   mask stops growing.
4. Failure signature: mask keeps gaining bits, out[1] keeps re-announcing bit2, no g4 frames.
5. Stop conditions: no H2D id5, no out[5] write, no misc-window read; if the device keeps
   re-entering vectors, capture the frames and stop (do not hammer 0xcece from a separate loop —
   one ack per observed event).

---

## 12. Reproduction (host-only; local artifacts, nothing vendor committed)

    # 1. disassemble both module versions (paths are local evidence, not committed)
    TC=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    $TC/objdump -dr --no-show-raw-insn hi5622v100_plat.ko > plat.s
    $TC/objdump -dr --no-show-raw-insn hi5622v100_wifi.ko > wifi.s

    # 2. annotate every relocation (strings/tables/function targets resolved)
    python3 tools/annotate.py hi5622v100_plat.ko plat.s plat.ann.s

    # 3. tables (g4 boot table, quit descriptor, dump ranges, ETE channel cfg, status table)
    python3 tools/tables.py hi5622v100_plat.ko .data 0x2660 5 12
    python3 tools/tables.py hi5622v100_plat.ko .rodata 0x101c 7 12

    # 4. call-site censuses used in this report
    python3 tools/census.py plat.ann.s pcie_msg_send,bal_msg_send,pcie_msg_register,bal_msg_register
    python3 tools/census.py wifi.ann.s hcc_msg_tx,hmac_send_msg

    # 5. symbol-set/version diff
    $TC/nm -n hi5622v100_plat.ko > plat.nm   # repeat for the 2.4.15 copy, then comm the (type,name) sets

**Evidence ledger (what a reviewer can re-derive with the commands above):**

| claim | artifact | address / offset |
| --- | --- | --- |
| release = 0x5a5a via bal_write | plat.ko .data | 0x26a4 descriptor; firmware_download 0xf834 tail |
| D2H handlers {1,2,3,4,5,6,7} | plat.ko text | pcie_msg_init 0xb6e4, wlan_pm_init 0xed30, exception_mgmt_res_init 0x1051c |
| g4 boot table | plat.ko .data | 0x2660, 5 x 12 B |
| g4 handlers | plat.ko | 0xdabc, 0xeb78, 0xebd8, 0x18c0 (.unlikely), 0x10214 |
| ack/drain/re-arm order | plat.ko | pcie_msg_handle 0x171f8 |
| wait out[0], not out[2] | plat.ko | pcie_msg_send_irq 0x174a8, pcie_msg_wait_for_clr 0x173dc |
| 200/2000 jiffies + timeouts | plat.ko | multi_chip_loading 0xf2ac |
| 100-byte read_msg object | plat.ko .bss / 2.4.15 symtab | 0x3be8; getter 0xe76c |
| 200-jiffy completer | plat.ko | device_plat_ready_msg_process 0xeb78 |
| 2000-jiffy completer | plat.ko | host_ready_msg_process 0xebd8 |
| ETE channel table | plat.ko .rodata | 0x101c, 7 x 12 B |
| dump-memory ranges | plat.ko .data | 0x26b8, 3 x 16 B |
| dev status table | plat.ko .rodata | 0xcac, 11 x 8 B |
| heartbeat 10 s | plat.ko | heartbeat_start 0x10074 (ktime literal) |
| g4 id2 = read_msg payload | wifi.ko import | wlan_get_d2h_read_msg undef |
| boot order | wifi.ko | host_module_init 0x3e164 |
| 2.4.15 symbol superset | 2.4.15 plat.ko | nm set diff (1958 vs 1763 names) |
