# HCC link/event bring-up: vendor host-side handshake determination + compile-ready host counterpart

**Lane:** wifi-hcc-link (shared task task-75), host-only, 2026-11-23.
**Status:** static reversal complete on both vendor module versions + both firmware-side
dispatch-table anchor sets; compile-ready host counterpart built and compile-verified;
bench procedure and stop rules written. No bench device was contacted; no image was
built/committed; no vendor binary, firmware byte-run or secret is committed here.

**Answer in one line:** the vendor host driver sends **no** HCC link-create /
link-register / event-register H2D message in any version or init path. The
firmware's per-link `+0x4B0` contexts and its event/tag layer are established by
firmware-internal boot code; nothing the host can post creates them. The host
counterpart delivered here is therefore a bounded **observability + H2D
consumer-liveness ladder** (`hcc_link_status` / `hcc_link_wake` / `hcc_link_plan`)
plus the negative proof, a bench procedure, and the explicit alternative lane.

Evidence markers: **[V]** verified here from a named local artifact (fresh
relocation-resolved call graph or fresh disassembly); **[V\*]** device-measured by an
earlier attended lane and cited; **[I]** inference; **[U]** unknown.

---

## 0. TL;DR

1. **No link/event H2D handshake exists in the vendor host** [V]. Full
   relocation-resolved sender census of *both* module versions
   (2.4.15 `hi5622v100_plat.ko` md5 `f688618b...` + `hi5622v100_wifi.ko` md5
   `1745ef77...`; 2.5.24 `plat` md5 `23660bc2...` + `wifi` md5 `4737fcb2...`):
   * `plat.ko` (the HCC/BAL/PCIe framework): 10-13 send sites, every one on a
     **runtime** path (timer timeout, xfer-done, ETE receive-buffer check, SR
     descriptor fill, PM sleep, exception);
   * `wifi.ko`: 72-76 send sites, every one a runtime config / mgmt / data-path
     event (`hmac_user_add`, `hmac_config_send_event`, scan/join/BA/CAC/spectral
     events, ...). None is a link create/register/event-register.
2. **The host-side HCC framework is host-RAM-local** [V]. `hcc_init`/`hcc_start`/
   `hcc_set_state`/`hcc_msg_register_tab_chip|core|customise` never reach a device
   write; `hcc_connect`/`hcc_disconnect` are five-instruction argument-check stubs;
   `bal_connect` is a `mov r0,#0; bx lr` stub. Nothing in the vendor driver
   "brings up" a device-visible HCC link.
3. **Init-path reachability is negative** [V]. From `init_module`,
   `host_module_init`, `hal_main_init`, `hmac_main_init`, `wal_main_init`,
   `hdpp_main_init`, `hmac_board_init`, `plat_hcc_init`, `hcc_init`, `hcc_start`,
   `bal_init`, `pcie_main_init`, `pcie_msg_init`, `multi_chip_loading`,
   `wlan_power_on`: the **only** init-path send is
   `hmac_board_init -> hdpp_chip_sync_add -> hmac_txq_table_init ->
   hmac_txq_table_addr_sync -> hdpp_config_send_event` (HCC group 0, id 1) - a
   post-DEVICE-READY TX-queue *config sync*, not a link bring-up. The only other
   reachable sender is on the module **exit** path
   (`hmac_main_exit -> ... -> hmac_config_del_vap -> hmac_config_send_event`).
   `plat.ko` init roots reach **no** sender at all.
4. **Firmware-side cross-reference is also negative for the boot fault** [V]. The
   firmware's 61 H2D receive handlers (wifi-h2d-catalog tables) were walked with a
   corrected recursive-descent call graph. No H2D handler reaches the firmware
   event dispatcher `0x7243C` or its per-link tag callbacks `0x745E0`/`0x7469C`;
   the link-create worker `0x46848` has no H2D caller (it is referenced from the
   firmware boot region's own dispatch data at CPU `0x474B8`, entry `0x46849`);
   the function containing the per-link bind `0x44BBC` (`0x437B0`) has exactly one
   H2D caller - the group-2 id-9 handler `0x627FC` (`hmac_send_mgmt_to_host`, a
   post-boot management/data-path message, not an init message).
5. **The observed H2D probe result is the decisive consistency check** [V\*]: on a
   cold boot the firmware does not consume H2D mailbox traffic at all
   (`out[0]` bit 3 stuck, `ack=0`, timeout `-110`). A host frame cannot be the
   missing input when the firmware's message layer never dispatches it. The S10
   data abort is firmware-internal sequencing (consistent with
   wifi-abort-rca sec 10.4 and wifi-boot-kick sec 21).
6. **Deliverable (compile-ready, built):** `files/pciet_hcc_link.{c,h}` +
   `patches/0002-hi5622-pciet-hcc-link-b3.patch` (applies on the wifi-h2d-inject
   0001 patch on the B3 tree; `tools/build.sh` builds it):
   * `hcc_link_status` (read-only): the exact boot-fault observability pack - g
     message object, HCC instance context `0x10C190`, destination-registry core-0
     record `0x171FB4`, the four per-link `+0x4B0` words (`0x172020/0x1722CC/
     0x172404/0x172458`), exception mask/state/boot-slot, mailbox words, ETE
     fill/consume indices; classifies `link_ctx=SET/UNSET` and prints the
     next-lane decision;
   * `hcc_link_wake` (write-only "1"): one bounded **vendor-attested payload-free
     mailbox id-1** wake (`out[0] |= BIT(1)`, `out[2] |= 1`) with a 100 ms
     consumption poll and a read-only `out[5]` checkpoint - the decisive
     "H2D consumer ALIVE vs DEAD" measurement, <=3 attempts per session;
   * `hcc_link_plan` (read-only): the ladder and stop rules as text.
   Module build evidence: `hi5622_pciet_b3.ko` sha256 `dab5ca76...`, vermagic
   `6.18.60 SMP mod_unload ARMv7 p2v8`, all `hcc_link_*` symbols present.
7. **Single next bench action:** after the standard boot procedure, run
   `cat hcc_link_status` (baseline) then `echo 1 > hcc_link_wake`. If the wake is
   **not consumed**, stop the H2D lane permanently and move to the
   **firmware-internal link/event bring-up lane** (sec 6) - no further host frame,
   payload or RAM write is justified. If it *is* consumed, the status line decides
   between "firmware-internal stage missing" (`link_ctx=UNSET`) and the
   driver-replica lane (`link_ctx=SET`).

---

## 1. Inputs, artifacts and method

| artifact | identity | use |
| --- | --- | --- |
| `hi5622v100_plat.ko` (2.4.15) | 371,708 B, md5 `f688618b4daa52473be41577f992f7d4` | host HCC/BAL/PCIe framework |
| `hi5622v100_wifi.ko` (2.4.15) | 3,581,748 B, md5 `1745ef77e728c411cef1921af3dc8731` | host hmac/hdpp/wal layers |
| `hi5622v100_plat.ko` (2.5.24) | 364,660 B, md5 `23660bc285393e678d5cade1c36c194b` | second version |
| `hi5622v100_wifi.ko` (2.5.24) | 3,564,728 B, md5 `4737fcb21a1a2262a96f84d780ad8b35` | second version |
| `FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20...` | firmware tables / call graph (local only, never copied) |

The 2.5.24 set is the same set phase37 analysed (md5 match); the 2.4.15 set is the
recovery-extract set the recent lanes use. Both were re-analysed here from scratch
so the result does not depend on one module version.

Method (tools committed under `tools/`, all read the local artifacts at runtime and
print derived annotations only):

* `tools/ko_xref.py` - **relocation-based** function call graph for the ARM
  `.ko` files. ET_REL kernel modules keep an `R_ARM_CALL`/`R_ARM_JUMP24`
  relocation for every cross-function call, so the caller/callee relation is
  exact (no branch-target heuristics; indirect calls are the only blind spot).
  Used for the sender census and the init-path reachability.
* `tools/fw_xref.py` - recursive-descent Thumb-2 call graph for FIRMWARE.bin
  (per-function visited set, capstone group-flagged branches, function-entry-aware
  tail calls). Used for the H2D-handler reachability.
* `tools/h2d_reach.py` - builds the firmware H2D receive roots from the two
  dispatch tables (verified in `incoming/wifi-h2d-catalog`) + the four mailbox
  handlers, then walks to the link/event targets.

Two tool bugs were found and fixed during this lane; they are documented because
they change what a "negative" call-graph result means:
1. stopping traversal on `ldr rX,[pc,#..]` (a literal-pool load) emptied most
   function bodies;
2. a *global* visited set shared across function descents left functions that had
   been touched by an earlier descent with an empty callee set, and a PC-return
   test compared against architectural register 15 instead of capstone's register
   name - both produced silent false negatives. The final tool's positives were
   validated on known boot-task call chains (`0x47628 -> 0x47D28`; mailbox id-1
   handler -> its worker) before the negative result was accepted.


---

## 2. The vendor host's complete H2D sender census (both versions)

Command (per module): `tools/ko_xref.py <ko> senders` (relocation-resolved
callers of `hcc_msg_tx`, `hcc_msg_tx_to_core`, `hcc_msg_alloc`,
`hcc_msg_alloc_no_cross_pool`, `hcc_msg_clone`, `hcc_msg_post_msg`,
`hcc_msg_post_vap_msg`, `hcc_queue_post_msg`, `hcc_queue_msg_process`,
`bal_msg_send`, `pcie_msg_send`, `pcie_msg_send_irq`). The last two are the
mailbox announce primitive (H2D mailbox id 3) and the deferred variant.

### 2.1 plat.ko (the HCC/BAL/PCIe framework) - every send is a runtime path

| sender | sends via | path class |
| --- | --- | --- |
| `hcc_timer_timeout_proc` | `hcc_msg_alloc`, `hcc_msg_tx` | runtime HCC timer expiry |
| `hcc_xfer_done` | `hcc_msg_tx` | runtime completion retry |
| `pcie_ete_rcv_buff_check` | `pcie_msg_send` | runtime receive-buffer failure branch (the **only** vendor id-5 poster - never on a success path) |
| `shuangta_ete_sr_dscr_fill` | `pcie_msg_send` | data-frame SR post (mailbox id 3) |
| `exception_handler`, `hcc_queue_msg_zero_len_debug`, `wlan_pm_sleep_work` | `bal_msg_send` | exception / debug / PM mailbox |
| `hcc_msg_alloc_no_cross_pool`, `hcc_msg_clone` | `hcc_msg_alloc` | allocator internals (not posts) |
| `hcc_msg_post_msg`, `hcc_msg_post_vap_msg` | `hcc_queue_post_msg` | queue helpers |
| `hcc_process_rx_thread`, `hcc_thread_process_all_msg` | `hcc_queue_msg_process` | receive-side dispatch (not posts) |

2.5.24 has the same set minus three *forwarding* helpers
(`hcc_msg_alloc_no_cross_pool`, `hcc_msg_post_msg`, `hcc_msg_post_vap_msg` are
absent in that build); **no version has a sender that looks like link
create/register/event-register**, and none of these is reachable from any init
root (sec 4).

### 2.2 wifi.ko - 72 (2.5.24) / 76 (2.4.15) send sites, all runtime config/data events

Full list in Appendix A. The 2.5.24 set is the 2.4.15 set minus four functions
(the same 72 names): `hdpp_send_deauth_msg`, `hmac_hcc_msg_tx_send`,
`shuangta_irq_hmac_send_exception_to_dmac_event`,
`shuangta_irq_host_ba_info_ring_overrun_isr`. Every name is one of:
user/vap lifecycle (`hmac_user_add`, `hmac_user_add_notify_alg`,
`hmac_del_user_notify_dmac`), config events (`hdpp_config_send_event`,
`hmac_config_send_event`, `hmac_config_alg_send_event`,
`hmac_config_h2d_send_app_ie`), scan/join/connect (`hmac_enc_scan_send_req`,
`hmac_handle_asoc_rsp_sta`, `hmac_handle_connect_rsp_ap`, ...), mgmt/BA/CAC/
spectral/CSI events, and the data path (`hmac_virtual_fill_tx_packet`,
`hmac_tx_ba_notify`, ...). **No link/register/event-register sender.**

The wire ids of these senders are the ones catalogued in
`incoming/wifi-h2d-catalog` sec 5 (group 0 ids 0x01..0x31, group 2 ids
0x01..0x26, one group-1 sender). Cross-referencing that catalogue with the
firmware receive tables was done by that lane (20/20 group-0 senders have a
firmware handler); what matters here is that **none of the senders is named,
sized or sequenced like a handshake** - every payload is a vendor config struct,
a mgmt frame or a data descriptor, and every sender is driven by a runtime
netlink/frame/scan event, not by driver init.

---

## 3. The host-side HCC framework is host-RAM-local (no device handshake)

This is the second half of the negative: the vendor does have an "HCC link/event"
layer - but it is entirely host-side bookkeeping. There is no device-visible
"link create" transaction to reproduce.

* `hcc_init` call graph (relocation-resolved):
  `bal_port_init, bal_register_hcc_callbacks, hcc_get_group_type,
  hcc_msg_module_init, hcc_queue_module_init, hcc_thread_init,
  hcc_timer_module_init, memset_s, printk` - no PCIe/BAL write call.
* `hcc_start`: `hcc_thread_init`, `hcc_timer_module_init`, then a local
  `strb r2,[r3]` of state 1 into a `.LANCHOR0` host-RAM byte. No device access.
* `hcc_set_state`: one `strb` to `.LANCHOR0`. `hcc_connect`:
  `cmp r0,#0 / movw r3,#0x8b2d / moveq r0,r3 / movne r0,#0 / bx lr` - an argument
  check, nothing else. `hcc_disconnect`: identical. `bal_connect`:
  `mov r0,#0; bx lr`.
* `hcc_msg_register_tab_chip` has exactly one caller in plat.ko
  (`plat_init_bal_hcc_excp`); `hcc_msg_register_tab_customise` only resolves
  host-RAM group resources (`hcc_get_group_res`) and writes host dispatch items.
  `hcc_msg_register_tab_core` has no caller at all in this build.
* The only device I/O the framework owns is the data-plane path: ETE SR/DR
  programming (`pcie_ete_*`), mailbox announce (`pcie_msg_send`) and the
  firmware-upload `bal_write` helpers (`firmware_download`, `firmware_file_send`,
  `dev_custom_send`). All of that is already replicated by our transport.

Consequence: the firmware cannot observe the host's HCC "link/event" state -
there is nothing for it to synchronise with. A firmware link must therefore be
created by firmware code; the host's role is only the transport service.

---

## 4. Init-path reachability: the only init-time H2D send is a post-ready config sync

Roots used (both versions): `init_module`, `host_module_init`, `hal_main_init`,
`hmac_main_init`, `wal_main_init`, `hdpp_main_init`, `hmac_board_init` (wifi.ko);
`init_module`, `plat_init`, `plat_hcc_init`, `hcc_init`, `hcc_start`,
`bal_init`, `bal_bus_init`, `pcie_main_init`, `pcie_msg_init`,
`pcie_comm_init`, `pcie_ete_init`, `wlan_power_on`, `multi_chip_loading`,
`plat_init_bal_hcc_excp`, `pcie_thread_init` (plat.ko).
Targets: every sender-caller function from sec 2 (the post primitives themselves
plus their forwarding helpers).

Result, identical in 2.4.15 and 2.5.24 [V]:

| root | first sender reached | classification |
| --- | --- | --- |
| `plat.ko` init roots (all 15) | **NO PATH** | plat.ko performs no H2D send during init |
| `hal_main_init`, `wal_main_init`, `hdpp_main_init` | NO PATH | - |
| `hmac_main_init` -> `hmac_board_init` -> `hdpp_chip_sync_add` -> `hmac_txq_table_init` -> `hmac_txq_table_addr_sync` -> `hdpp_config_send_event` | group 0 id 1 config sync | **post-DEVICE-READY** (hmac_main_init runs after multi_chip_loading's ready waits); TX-queue table address sync, not link bring-up |
| `init_module` / `host_module_init` -> `hmac_main_exit` -> ... -> `hmac_config_del_vap` -> `hmac_config_send_event` | group 0 id 1 | module **exit** path, not init |

The group-0 id-1 config-sync structure is also confirmed on the wire side:
`hdpp_config_send_event` builds a 12-byte header + payload with
`hdr[0]=0` (group 0), `hdr[6]=1` (id 1), `hdr[1] high nibble from the source
core, `hdr[2]/`hdr[3] from the descriptor, then payload `+0x8` = 16-bit key,
`+0xA` = 16-bit length, `+0xC` = data; the firmware group-0 id-1 handler
(CA 0x43D59) reads exactly payload `+8` (`u16` key), `+0xA` (`u8`) and
`+0xC` (data pointer) - a key-driven config worker, not a link/event handshake.

So even if the boot gate were passed and the driver proceeded, the first host
H2D message would be a TX-queue config sync **after** DEVICE READY - it cannot be
the missing input for a boot-stage link-context fault.


---

## 5. Firmware side: no H2D receive path drives the link/event layer

### 5.1 What the firmware's H2D receive tables are (cross-reference)

The firmware's own H2D HCC dispatch tables (byte-verified in
`incoming/wifi-h2d-catalog` sec 4) are: group 2 = 19 ids (table CA `0x10CC18`),
group 0 = 40 ids (table CA `0x10CCFC`), one early group-0 id `0x2F` (table CA
`0x10CBEC`). Their handlers are configuration/data-path message processors.
Nothing in the tables is named or shaped like a link/service registration: the
handlers read per-message payload structs (user records, config keys, mgmt
frames) and dispatch to per-key workers.

### 5.2 Reachability from every H2D receive root (corrected call graph)

Roots: the 61 HCC receive handlers (group 2/0/early) + the four mailbox handlers
(ids 1/3/5/6, 5 = BUG trap). Targets: the link/event machinery named by the lane
brief and by abort-rca - link-create worker `0x46848`, per-link bind function
`0x437B0` (contains the `0x44BBC` store), event tag dispatcher `0x7243C`,
per-link tag callbacks `0x745E0`/`0x7469C`, and the descriptor path
`0x88AFC`/`0xDA6DC`.

Result (`tools/h2d_reach.py`, 62 roots / 8 targets): **5 paths, none to the
event layer:**

| root (HCC group/id) | vendor sender meaning | path |
| --- | --- | --- |
| g2 id 0x02 (fn `0xB9780`), g2 id 0x05 (same fn) | hmac_handle_scan_rsp_sta / hmac_handle_connect_rsp_ap | `0xB9780 -> 0x914CC -> 0x8F77C -> 0x63988 -> 0x61998 -> 0xA52C0 -> 0xDA6DC -> 0x88AFC` |
| g2 id 0x03 (fn `0xBBC9C`) | hmac_handle_asoc_rsp_sta / hmac_report_connect_failed_result | `0xBBC9C -> 0xBA1C8 -> 0xB9780 -> ... same ...` |
| g0 id 0x0C (fn `0x6494C`) | hmac_enc_scan_send_req / wal_netdev_set_mac_addr | `0x6494C -> 0x644A4 -> 0x63CB8 -> 0x5E028 -> 0xA52C0 -> 0xDA6DC` |
| g0 id 0x16 (fn `0x62D88`) | hmac_mgmt_tx_event_status | `0x62D88 -> 0x616EC -> 0xA52C0 -> 0xDA6DC` |
| g2 id 0x09 (fn `0x627FC`) | hmac_send_mgmt_to_host | `0x627FC -> 0x437B0` |

Interpretation:

* The four `0xA52C0` paths enter the per-link **data path** helper (which guards on
  `[obj+0x4B0]` and then calls `0xDA6DC` / `0x88AFC`). They are post-boot TX/RX
  paths: they require a live user/link object to be selected by the message's
  header field; they are not a bring-up sequence and every one of them is a
  normal data-path send.
* The g2/id-9 path reaches the big function `0x437B0` that contains the per-link
  bind store `0x44BBC`. This is `hmac_send_mgmt_to_host` - a management-frame
  data-path message. It is not an init/link-create message; the store executes
  only on the function's link-setup branch with a live object.
* **No root reaches `0x7243C` (event tag dispatcher), `0x745E0`/`0x7469C` (its
  per-link callbacks) or `0x46848` (link-create worker).**

### 5.3 What the event callbacks and the dispatcher actually are

Disassembly of the dispatcher and its two callbacks shows a firmware-internal
per-link **TX/RX data-path event layer**, not a host-facing service:

* `0x7243C`: `push {r0-r2,r4-r7,lr}`; validates `(ctx,msg)`; reads the event tag
  `u16 [msg+0]`; rejects tags outside `0xDAD..0xDC9`; walks the static 25-entry
  `{tag,fn}` table at CPU `0x104CB8` and tail-calls `fn(ctx, msg)`.
* `0x745E0` (tag `0xDBD`) / `0x7469C` (tag `0xDBC`): `r4 = [ctx+0x4B0]`; if null
  they log (lines `0x29F` / `0x272`) and return `0x64`; otherwise they derive a
  per-user/per-vap offset pair from `[r4]`/`[r4+1]` with strides `0xFC0` and
  `0x7E0`, write descriptor fields at `r4+0xDF8..0xDFD` / `+0xDE0..0xDE4` and
  call the TX/RX workers (`0xE9A7C`, `0xEB268`, ...). That is a data-frame
  event carrying a per-user index, exactly the kind of event the firmware sends
  itself during traffic.
* The dispatcher is registered by a **static** firmware service descriptor at
  CPU `0x106E40` (12-byte rows `{u32 count, u32 fn, u16 tag_lo, u16 tag_hi}`; the
  row `0x106E70` is `{0xF, 0x7243D, 0xED8, 0xEDB}`) and has no in-image literal
  reference - it is called indirectly from firmware event code, not from the H2D
  message tables.

### 5.4 What actually creates the per-link `+0x4B0` contexts

* The `+0x4B0` store family in the image is `0x48840` (inside the link-create
  worker `0x46848`: `o = 0x961AE(byte[r5]); o+0x4B0 = [sb+8]`) and `0x44BBC`
  (inside `0x437B0`), plus the teardown `0x4490E` and two unrelated ops-table
  initialisers (`0x59F50`, `0x5CFF0`). [V, re-verified here]
* `0x46848` has **no BL caller anywhere in the image**. Its only in-image pointer
  reference is a thumb pointer `0x46849` inside a boot-region pointer table at
  CPU `0x474B8..0x47520` (the table also carries other boot entries such as
  `0x47625` and RAM pointers `0x172A88/0x172DE8/0x17DEA8`). [V]
* `0x437B0`'s only direct H2D caller is the g2/id-9 handler above; its other
  callers are firmware-internal.

So the per-link creation/bind family is firmware bootstrap code called from the
firmware's own boot dispatch, not from a host message. The boot-region table's
consumer was not located (no literal reference is used to reach it - it is
indexed by the boot dispatcher itself), so **which boot entry/stage failed to
run the bind remains an open firmware-side question** - but it is not a host
message.

### 5.5 Consistency with the observed cold-boot state [V\*]

The boot-kick/abort-rca measurements are exactly what this negative predicts:

* H2D mailbox probe: `out[0]` bit 3 stays set, `ack=0`, timeout `-110` - the
  firmware's H2D dispatcher never runs, so **no H2D frame - ours or a
  reconstructed vendor one - would be consumed** on the current cold boot.
* The per-link `+0x4B0` words `0x172020/0x1722CC/0x172404/0x172458` read ZERO;
  the faulting callback context therefore has a null per-link pointer. Since no
  boot-time host message touches those objects, they must be populated by the
  missing firmware stage (the one tied to the boot-region dispatch), not by the
  host.

---

## 6. Deliverable: compile-ready host counterpart

See **SPEC.md** for the full specification and **BENCH-PROCEDURE.md** for the
attended ladder. Summary:

| file | contents |
| --- | --- |
| `files/pciet_hcc_link.h` | contract constants (region window rules, observability CAs, mailbox ids, wake bounds), result codes, entry points |
| `files/pciet_hcc_link.c` | `hcc_link_status` (RO), `hcc_link_wake` (WO), `hcc_link_plan` (RO); probe/remove integration; build-time safety asserts |
| `patches/0002-hi5622-pciet-hcc-link-b3.patch` | add-on patch for the B3 tree **on top of** wifi-h2d-inject's `0001` patch (two new files + three integration edits: header prototypes, probe/remove calls, Kbuild object) |
| `tools/apply-edits.py`, `tools/gen-patches.sh` | deterministic patch regeneration |
| `tools/build.sh` | applies 0001 + 0002 to a copy of the B3 tree and builds the external module (same kernel tree/toolchain as the h2d lane) |
| `tools/ko_xref.py`, `tools/fw_xref.py`, `tools/h2d_reach.py` | the reversal tools that produce the evidence in this report |
| `tools/census.sh` | one-shot regeneration of the sender census tables |

Build verification (host-only, module-only; no image build):

    opensource/docs/incoming/wifi-hcc-link$ ./tools/build.sh
    ...
      CC [M]  pciet_hcc_link.o
      LD [M]  hi5622_pciet_b3.ko
    -rw-r--r-- ... hi5622_pciet_b3.ko
    sha256  dab5ca7676f2706f0c812c05698528ef00017de9491047b80439fe1503b2ae6f
    vermagic=6.18.60 SMP mod_unload ARMv7 p2v8
    == hcc_link surface present in the module:
    dev_attr_hcc_link_plan / dev_attr_hcc_link_status / dev_attr_hcc_link_wake

Safety properties encoded in the code (defence in depth):

* **never** writes CA `0x400392F0` (out[5]) - build-time `BUILD_BUG_ON` asserts +
  the transport's single gated writer `pciet_writel_checked()`; out[5] is only
  read back as a checkpoint;
* **never** posts mailbox id 5 (the firmware BUG trap); the only mailbox bit the
  wake probe writes is `BIT(1)`;
* the wake probe refuses before CPU release, refuses when `out[0] != 0`, is
  limited to 3 attempts, and is poll-bounded (100 ms);
* **no device RAM/IO write other than out[0]/out[2]**; the four per-link
  `+0x4B0` words and all other firmware objects are read-only;
* reads are bounded to the two mapped windows (region-5 CPU RAM
  `BAR0 = CPU + 0x6B8000`, CPU < `0x198000`; region-3 IO
  `BAR0 = 0x3B8000 + (CA & 0xFFFFF)`); the RC misc window `0x10161000` is never
  touched and UART is never used.

### 6.1 Why this is the host counterpart even though it sends no "handshake"

The vendor host's link/event bring-up is local bookkeeping (sec 3) and its first
device-visible H2D message is a post-ready config sync (sec 4). The only
host-side action that can *cause* the firmware to establish link/event state at
boot would be something the firmware reads before/while its boot flow runs; the
candidates are already covered by the transport (ETE ring programming, DR free
buffers, mailbox service, firmware upload/release, gate service), and the
remaining boot-stage state is firmware-internal (sec 5.4). What the host can
still do reproducibly and safely is **measure** whether the firmware is at the
point where any H2D message could matter, and **classify** the link/event state.
That is exactly the ladder delivered here; it produces the decision that selects
the next lane instead of adding an unproven frame.

---

## 7. Alternative lane (item 4 of the task) - the negative's consequence

Because the vendor driver provably does not send a link/event bring-up message,
"full driver/thread replica" is **not** the fix for the missing firmware per-link
contexts: the host-side HCC layer the replica would implement is host-RAM-only
and invisible to the firmware, and the vendor driver never posts anything at
boot. The evidence points at a firmware-internal stage. Recommended next lanes,
in order:

1. **Firmware-internal link/event bring-up lane (primary).** Identify the
   consumer of the boot-region pointer table at CPU `0x474B8` (entries include
   the link-create worker `0x46849` and boot entries `0x47625`) and the boot
   stage that should execute the `+0x4B0` bind for the four registry targets
   (`0x172020/0x1722CC/0x172404/0x172458`). Concrete steps: decode the table
   layout and every entry's callee; find the code that indexes it; map it onto
   the boot-state machine (CPU `0x47628` and the prelude at `0x47ADC..0x47B2A`);
   identify the entry whose execution would bind the per-link contexts; compare
   with the observed state after the S10 abort. Also inspect the firmware
   event-thread loop entry points from boot-kick sec 19.2
   (`0xC630A/0xC63B4/0xC68FE/0xC71F4/0xBF102`) for the first stage that would
   dispatch a tag event.
2. **Vendor-stack golden trace (diagnostic only).** On a 5.10 vendor system
   (stock image, attended) instrument the vendor modules (there are
   `plat_debug.ko`/`wifi_debug.ko` debug companions) to record the real init
   order and observe when/where the per-link `+0x4B0` words become nonzero. This
   is read-only diagnostic evidence for lane 1; it is not a host patch and must
   not be ported as code.
3. **Do not** fabricate the per-link pointer or registry words (abort-rca
   sec 10.4 external-abort risk), and do not send speculative payload frames:
   with the H2D consumer dead (sec 5.5) they cannot even be dispatched, and some
   handlers dereference payload words.

---

## 8. Bench procedure (short form; full form in BENCH-PROCEDURE.md)

Preconditions (project hard rules): human at the bench, recovery door armed,
bench link PASS, UART read-only, no image build, no rmmod after release.

1. Standard boot procedure of the B3 instrument, unchanged:
   `insmod hi5622_pciet_b3.ko poll_ms=0` (built from this lane's
   `tools/build.sh` output, or from the h2d-inject tree + this lane's patch);
   prepare (6/6); upload; verify (diffs=0); arm (P0 7/7); then the C1e-style
   service loop (`out[1]` drain/ack/re-arm + `0xcece` gate) and release
   `0x5a5a`.
2. **L0** `cat /sys/bus/pci/devices/<dev>/hcc_link_status` - record the whole
   output (baseline; all reads).
3. **L1** `echo 1 > .../hcc_link_wake` - one payload-free mailbox id-1 wake.
   * consumed (`out[0] bit clears; `out[2]` reads 8; `out[5]` reads 1) =>
     H2D consumer ALIVE;
   * `-ETIMEDOUT` / `out[0]` unchanged => H2D consumer DEAD: **stop the H2D
     lane**; no frame or payload experiment can be the missing input.
4. **L2** `cat hcc_link_status` again - classify `link_ctx` from the four
   `+0x4B0` words and `registry` from the core-0 record shape.
   * consumer ALIVE + `link_ctx=0/4 UNSET` => firmware-internal bring-up still
     missing (sec 7 lane 1); do not fabricate RAM.
   * consumer ALIVE + `link_ctx=4/4 SET` => link/event contexts present;
     continue with the driver-replica lane.
   * consumer DEAD => sec 7 lane 1 regardless of `link_ctx`.
5. **L3 (optional, only with the h2d injector loaded, armed and `out[0]==0)**:
   one payload-free group-0 id-0 frame (`echo 'group=0 id=0 dst=0' >
   .../h2d_inject`) as the minimal "can the firmware's own HCC dispatch be
   reached at all" liveness check. Never id 5, never out[5].

Stop rules: <=3 wake probes/session; do not clear `out[0]` after a timeout (it
hides a dead consumer); anomaly (new `out[1]` bits, staged abort frame changes,
oops) => power-cycle, no in-session retry; never write device RAM or the per-link
`+0x4B0` words.

---

## 9. Reproduction (host-only; local artifacts only)

    PY=<repo>/.provision-venv/bin/python
    M=/home/shibbityshwab/recovery-extract/lib/hisilicon/ko        # 2.4.15 set
    N=/tmp/rootfs2524b/lib/modules/5.10.201                       # 2.5.24 set
    FW=/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin

    T=opensource/docs/incoming/wifi-hcc-link/tools
    $PY $T/ko_xref.py $M/hi5622v100_plat.ko senders          # -> 13 lines
    $PY $T/ko_xref.py $M/hi5622v100_wifi.ko senders          # -> 76 lines
    $PY $T/ko_xref.py $N/hi5622v100_plat.ko senders          # -> 10 lines
    $PY $T/ko_xref.py $N/hi5622v100_wifi.ko senders          # -> 72 lines
    $PY $T/ko_xref.py $M/hi5622v100_wifi.ko reach \
        init_module,host_module_init,hal_main_init,hmac_main_init,wal_main_init,hdpp_main_init,hmac_board_init \
        hdpp_config_send_event,hmac_config_send_event        # post-ready/exit only
    $PY $T/ko_xref.py $M/hi5622v100_plat.ko reach \
        init_module,hcc_init,hcc_start,bal_init,pcie_main_init,pcie_msg_init,multi_chip_loading \
        hcc_timer_timeout_proc,hcc_xfer_done,pcie_ete_rcv_buff_check,shuangta_ete_sr_dscr_fill,exception_handler   # NO PATH
    $PY $T/h2d_reach.py                                      # 5 paths, none to the event layer
    ./tools/build.sh                                         # compile-ready module
    ./tools/gen-patches.sh                                   # regenerate patch 0002

The firmware tools accept `--fw` (default `$HI5622_FW` or the local path) and use
the CPU-address convention `CPU = file offset + 0x40000`.

---

## 10. Evidence index

| claim | artifact / method | grade |
| --- | --- | --- |
| sender census (both versions) | relocation call graph of the four .ko files (`ko_xref.py senders`) | [V] |
| host HCC framework local-only | `hcc_init/hcc_start/hcc_connect/hcc_msg_register_tab_*` disassembly + call graph | [V] |
| init-path H2D negative | reachability from the 22 init roots to all sender functions, both versions | [V] |
| post-ready config sync (group 0 id 1) | `hdpp_config_send_event` disassembly + `hmac_board_init` chain | [V] |
| H2D receive-root reachability | 62 roots x 8 targets, corrected recursive-descent Thumb graph (`h2d_reach.py`) | [V] |
| event dispatcher / callbacks are per-link data path | `0x7243C`, `0x745E0`, `0x7469C` disassembly; static table `0x104CB8`; descriptor `0x106E40` | [V] |
| link-create worker `0x46848` has no H2D caller | full-image BL scan + software breakpoint-free call graph; only in-image pointer `0x46849` in the boot table at `0x474B8` | [V] |
| H2D probe not consumed | earlier attended lane: `out[0]` bit 3 stuck, `ack=0`, `-110` | [V\*] |
| module build | `tools/build.sh`: sha256 `dab5ca76...`, vermagic 6.18.60 | [V] |

---

## Appendix A - wifi.ko sender names (union of both versions, 76)

`hdpp_config_send_event, hdpp_send_deauth_msg, hmac_acs_init_scan_hook,
hmac_ba_timeout_fn, hmac_cfg_tx_sched_switch, hmac_chan_do_sync,
hmac_chan_initiate_switch_to_new_channel, hmac_chan_restart_network_after_switch,
hmac_config_alg_send_event, hmac_config_h2d_send_app_ie,
hmac_config_receive_all_sta_rssi, hmac_config_send_event, hmac_csi_complete,
hmac_del_user_notify_dmac, hmac_dfs_cac_stop, hmac_edca_opt_timeout_fn,
hmac_enc_scan_send_req, hmac_event_acs_response, hmac_handle_asoc_rsp_sta,
hmac_handle_connect_rsp_ap, hmac_handle_disconnect_rsp_ap,
hmac_handle_scan_rsp_sta, hmac_hcc_msg_tx_send, hmac_hsan_event_report,
hmac_mgmt_reset_psm, hmac_mgmt_rx_addba_rsp, hmac_mgmt_rx_delba,
hmac_mgmt_send_deauth_frame_notify, hmac_mgmt_tx_ampdu_end,
hmac_mgmt_tx_ampdu_start, hmac_mgmt_tx_event_status, hmac_multiap_event_report,
hmac_multiap_handle_steering_req_msg, hmac_multiap_parse_chan_sel_req_msg,
hmac_report_cac_finish, hmac_report_cac_start, hmac_report_connect_failed_result,
hmac_report_external_auth_req, hmac_rx_data_send_continue_msg,
hmac_rx_data_send_disasoc_frame_notify, hmac_rx_mic_failure_process,
hmac_scan_config_bss_color, hmac_scan_proc_scan_req_event_exception,
hmac_scan_proc_sched_scan_req_event, hmac_sdt_recv_reg_cmd,
hmac_send_connect_result_to_dmac_sta, hmac_send_mgmt_to_host, hmac_send_msg,
hmac_spectral_scan_complete, hmac_spectral_scan_timeout_fn,
hmac_sta_handle_disassoc_rsp, hmac_sta_sync_join_req_params,
hmac_sta_up_update_edca_params_machw, hmac_sta_up_update_mu_edca_params_machw,
hmac_sta_wait_join, hmac_sta_wait_join_rx_beacon, hmac_tx_ba_notify,
hmac_tx_complete_notify_other_core, hmac_tx_complete_read_error_process,
hmac_tx_msdu_rw_err_proc, hmac_tx_post_event, hmac_user_add,
hmac_user_add_notify_alg, hmac_vip_frame_event_post, hmac_virtual_fill_tx_packet,
shuangta_irq_hmac_send_exception_to_dmac_event, shuangta_irq_host_all,
shuangta_irq_host_ba_info_ring_overrun_isr, shuangta_irq_host_mac_isr,
wal_acs_netlink_recv, wal_alloc_cfg_event, wal_dfr_excp_rx,
wal_netdev_set_mac_addr, wal_recv_config_cmd, wal_send_cfg_event_msg_tx,
wal_wlan_cfg_module_host_process_entry`

(Registry helpers `wal_alloc_cfg_event`, `wal_recv_config_cmd`,
`wal_send_cfg_event_msg_tx`, `wal_wlan_cfg_module_host_process_entry`,
`hmac_send_msg`, `hmac_hcc_msg_tx_send` post via callers and are not independent
message ids. The `shuangta_irq_*` functions are interrupt-context post helpers.
None of the 76 is an init-time link/event handshake.)

## Appendix B - firmware CPU addresses used in this report

| object | CPU | note |
| --- | --- | --- |
| H2D receive tables | `0x10CC18` (g2, 19), `0x10CCFC` (g0, 40), `0x10CBEC` (early, 1) | wifi-h2d-catalog sec 4 |
| event tag dispatcher | `0x7243C` | called `fn(ctx,msg)`, tags `0xDAD..0xDC9` |
| tag callbacks | `0x745E0` (0xDBD), `0x7469C` (0xDBC) | read `[ctx+0x4B0]`, per-link TX/RX |
| static tag table | `0x104CB8` | 25 x `{u32 tag, u32 fn}` |
| service descriptor | `0x106E40` | 12-byte rows, row `0x106E70` = `{0xF, 0x7243D, 0xED8, 0xEDB}` |
| link-create worker | `0x46848` | contains `0x48840` (`o+0x4B0=[sb+8]`) |
| per-link bind function | `0x437B0` | contains `0x44BBC` (`+0x4B0/+0x4B4/+0x4B8`) |
| descriptor path | `0x88AFC` -> `0x87D64`, `0xDA6DC` | abort-rca chain |
| boot region pointer table | `0x474B8` | holds `0x46849`, `0x47625`, RAM pointers |
| g message object | ptr `0x172130`; fields `+0x9C/+0xB4/+0xB8/+0x2CC` | wifi-boot-kick/abort-rca |
| HCC instance context | `0x10C190` | `g+0x9C` = pointer slot |
| destination registry core-0 record | `0x171FB4` (8 words) | live: `{0,0x103FB8,0x171B70,0x171E1C,0x171F54,0x171FA8,0,0}` |
| per-link `+0x4B0` words | `0x172020, 0x1722CC, 0x172404, 0x172458` | all ZERO on the parked cold boot |
| mailbox words | out[0] `0x40039010`, out[1] `0x40039014`, out[2] `0x400392D4`, out[3] `0x40101438`, out[4] `0x40101414`, out[5] `0x400392F0` (never write), D2H doorbell `0x40101434` | hcc-re sec 5.1 |

