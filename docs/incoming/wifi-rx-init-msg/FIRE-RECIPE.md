# FIRE-RECIPE - the candidate RX-init config message (key 0x0004), attended R2

**Lane:** `rx-init-decode` (task-170). **Host-only deliverable; the Lead fires.** This recipe is
the discriminator for the one open bug (device-side RX capture: MAC receives, bit 10 latches,
`frames=0`, intermittent page loss). It adds **one** host->firmware message and changes no
register, ring or buffer.

**PASS** = RX frames captured (`frames>0`) and the golden page survives.
**FAIL** = `frames=0` and/or bit-10 latch (`0x582`) and/or `page golden:0`.
A FAIL refutes the candidate; do not repeat it in the same power-on.

---

## 0. Artifact

| item | value |
| --- | --- |
| base tree | `opensource/docs/incoming/wifi-h2d-send/src` (committed clean H2D carrier tree; the fired association tree is a later descendant - see the port note) |
| patch | `patches/0001-rx-init-msg.patch` (applies with `git apply -p1` at the `src` parent) |
| new module params | `rx_init_enable=1` (fail-closed default 0; allows ONE `h2d_rx_init` write per load) |
| new sysfs node | `h2d_rx_init` (WO; accepts only `1`) |
| build | `tools/build.sh` of the wifi-h2d-send lane (host selftest + out-of-tree module build), from the patched tree |
| expected artifact | `hi5622_wifi.ko` - **size/sha256 to be pinned after the build** (PENDING PIN); the patch should keep the compiler/modpost warning count at 0 |

Port note for the fired association tree: the fired tree's carrier is the later
association/mgmt-carrier variant. The three content hunks to port are (a) the enum + name/parse
entry for `rx_init`, (b) the builder case (g0 id1 key `0x0004`, len 4, data u32 1, fb = the
created module 4, fa/fc 0, token/tail from the ctx_create frame), (c) the one-shot gate
(requires `ctx_created`, refuses a second send) + the WO attribute. Everything else (SR node
commit, mailbox announce, bounded out[0] read-back) is the tree's existing `h2d_post` path.

## 1. Preconditions (STOP if any fail)

* Attended session, recovery door armed (`tools/recovery-card.sh`), bench link PASS.
* **Fresh power-cycle** (the chip exception state only clears by power-cycle; a SoC reboot does
  not reset the Wi-Fi chip).
* `FW = hi_wifi/FIRMWARE.PD4.bin` (the patched firmware that makes our H2D messages get
  processed; the stock firmware refuses them with -16).
* One module load per boot. The ordered ladder must be exactly: boot ladder -> C2 ctx_create ->
  **AP FIRST** -> dp_probe -> dp_setup -> h2d_txq_addr_sync -> **rx_init** -> dp_rx_arm.
  One AP profile per boot; RX arm is one-shot (re-arm is a MAC re-init hazard).

## 2. Load line (planned; adjust only the budget if the tree's default differs)

```
insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin \
       rx_enable=1 rx_buf_class=1 rx_refill_wm=75 \
       h2d_send_budget=8 rx_init_enable=1
```

(`rx_buf_class=1` = the WOC buffer class already tested; `rx_refill_wm=75` = the watermark
module default; `h2d_send_budget=8` covers the ladder's frames. `rx_init_enable=0` = control
load, no candidate message.)

## 3. Fire sequence (S0-S7)

| step | command (write) | expected read-back | STOP if |
| --- | --- | --- | --- |
| S0 | fresh power-cycle; load stock? no - load PD4 module with the line above | `dmesg`: probe + boot ladder start | probe fails |
| S1 | boot ladder (tree's canonical commands) | boot latched, core phase `READY_TXQ_SYNC`, `golden page 0x86807fef/...` | phase FAILED or golden:0 |
| S2 | C2 `ctx_create` | `H2D ctx_create ... CONSUMED after N polls`; `ctx_created=1` in status | not consumed / lane closed |
| S3 | AP FIRST: `vap_add -> ap_chan -> ap_ssid -> ap_cfg -> vap_start` | 5/5 consumed, AP on air (scan shows the SSID) | any frame not consumed |
| S4 | `dp_probe`; `dp_setup`; `h2d_txq_addr_sync` | rings + MAC program OK, page golden | page lost |
| S5 | **exit the AP config path (return AP FIRST to the bench state)** | - | - |
| S6 | `printf 1 > .../h2d_rx_init` | dmesg: `H2D rx_init: frame 32 B ... CONSUMED after N polls`; the announce read-back is the existing out[0] poll; then `dp_rx_arm` | write refused (expected reason printed: `no_ctx_create`/`rx_init_already_sent`/`out0_not_consumed`) -> do NOT retry; the gate is the safety |
| S7 | laptop auth: ONE `iw connect`/`wpa_supplicant` attempt (=3 kernel auth tries), up to 3 attempts | capture below | chip exception / page golden:0 -> stop, power-cycle |

Read-only captures after S7 (record verbatim):

```
cat /sys/devices/.../dp_rx_probe      # actions / frames / bad / chains  (the frames>0 proof)
cat /sys/devices/.../h2d_status      # frames_sent, gate names, last gate
cat /sys/devices/.../dp_plan         # live bank + status word before/after
dmesg | tail -80                     # announce consumed lines, CRX g1 id13, exception lines
# golden page check: the tree's page/mask status attribute (status 0x00000000 mask 0x04000010)
cat /sys/kernel/debug/... (or the tree's dp_rx_parity/status attr)   # status word + golden
```

## 4. PASS / FAIL matrix

| outcome | meaning | next |
| --- | --- | --- |
| **PASS**: `dp_rx_probe` `frames>0` AND page golden:1 (and ideally CRX g1 id13 in dmesg) | key 0x0004 context byte was the missing firmware config | record the exact byte value used (1), the frame hexdump from dmesg, and the status transition; the fix graduates from candidate to confirmed; next lane folds it into the final image |
| FAIL-A: `frames=0`, page stays golden, status stays 0x182 | message consumed but no effect | candidate refuted for the RX enable; next = F4 status-forwarding class (g0 id0 is the safe stub) |
| FAIL-B: status bit10 latches (0x582) and/or page golden:0 during/after the auth attempts | the receive path still faults with the extra message | stop; power-cycle; record the exception mask + page gate; candidate refuted |
| FAIL-C: write refused by the gate | ladder order violated (e.g. ctx_create not consumed) | fix the order, do not raise the gate |

## 5. Controls (cheap, do them if time permits)

* **Control load**: same boot sequence with `rx_init_enable=0`; the `h2d_rx_init` write must be
  refused with `-EPERM` and nothing posted.
* **Negative key**: not supported by the patch deliberately (allowlist = 0x0004 only) - do not
  add one on the bench.
* Do NOT send key 0x0004 twice: the one-shot gate must print `rx_init_already_sent`.

## 6. What remains (honest)

* Module sha256/size PENDING; the patch must be ported onto the fired association tree (3 content
  hunks, section 0) and built there before the fire.
* The candidate is a discriminator, not a proven fix: the static decode says key 0x0004 has no
  sender in the shipped vendor module, so the vendor might never send it (its firmware worker then
  writes a context byte we cannot see read anywhere yet - REPORT.md sec 4.3/5.1).
* If the fire is not available, the fallback experiments are unchanged: F4 forwarding class, the
  stock-boot H2D trace (decisive vendor reference), and a deeper DMAC/firmware completion-descriptor
  decode.
