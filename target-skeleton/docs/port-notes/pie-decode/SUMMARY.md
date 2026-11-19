> Integration note: this directory is an **incoming** deliverable. Apply
> `shared.patch` (below) to the current repository and commit it together with
> `target/linux/hisilicon/patches-6.18/024-net-pie-completion-decode-luofu.patch`; the
> SUMMARY is the durable record of the lane.

# incoming/pie-decode - PIE RX completion decode + TX descriptor (patch 024)

**Task:** task-56 (lane `pie-decode`, agent `pie-decode`). **Date:** 2026-11-19.
**Scope:** decode the vendor PIE v250 RX completion descriptor and the normal non-TSO TX
descriptor from the local `hi_kpie.ko` fixture (host-only disassembly; no vendor binary copied),
implement the decode + RX delivery and the full normal TX descriptor in the PIE/GEMAC drivers as
patch `024` behind the existing `luofu_pie.arm=1` arm gate, and validate the `001`-`024` series
in an isolated `openwrt main` clone. **No device was touched** (no router, UART, TFTP, flash or
bench tool) and no PIE register has ever been written. Host-only, evidence-graded.

---

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/024-net-pie-completion-decode-luofu.patch` | RX completion decode + NAPI delivery, RX port registration, full v250 normal non-TSO TX descriptor, ring-index pointer semantics; GEMAC reads `hisilicon,port` and registers/unregisters its RX port; binding updates | `d81a44fb7c004e66d9c686d0d38ee5cccdf52e8ba6f9e454a92f091352ae36f1` |
| `target-skeleton/docs/incoming/pie-decode/shared.patch` | board DTS (`hisilicon,port` 8..12 on the five MAC nodes), README (patch-024 row + status), SKELETON-SMOKE (session), PIE-DATAPATH (decoded descriptor tables, flows, unknowns), TARGET-DESIGN rows; diffed against `opensource` HEAD `5a25d39` (the 026 integration) so it applies on top of 024/025/026 docs | `a2b37a2dafa5f290255dc5d92d6c02c29b4e5e8a05d59f6f22257acdc81ad38c` |
| `target-skeleton/docs/incoming/pie-decode/SUMMARY.md` | this file (moved to `port-notes/pie/SUMMARY.md` on integration) | - |

---

## 2. What the decode recovered (evidence map)

All of this is **read-only disassembly of the parent-repository fixture**
`rootfs-2.4.15/lib/hisilicon/ko/hi_kpie.ko` (68,524 B, sha256
`333cb968eed464ebab89636fa31d9db43db2b5c2766417d3e380e4a827590072`, identical in 2.5.24), plus
the consumer code paths and the 2026-11-18 measured inventory for the ring-pointer values. The
full tables are in `docs/PIE-DATAPATH.md` section 5.2 and the flows in section 6.

### 2.1 RX completion descriptor (v250)

| offset | field | how it was proven |
| --- | --- | --- |
| +0 u16 | frame length | `hi_pie_rx_get_desc@0xf14` copies it into summary +4; `hi_pie_drv_rx_recv@0x3870` calls `skb_put(skb, u16 summary+4)` |
| +3 bit 2 | owner (driver sets on arm, engine clears on completion, direction [I]) | `hi_pie_rx_set_owner@0xefc` `bfi #2,#1`; consumer reads completions by read pointer, not owner |
| +4 u32 | buffer DMA address (driver-written) | `hi_pie_rx_set_addr@0xf0c` |
| +8 bits 6:0 | status/error code, 0 = no error | `rx_get_desc` `ubfx #0,#7` -> summary +6; summary +7 bit 0 = (field == 0); `hi_net_rx_proc@0x6568` drops when bit 0 clear |
| +9 bits 7:4 | port on the error path | `rx_get_desc` error branch |
| +10 bits 5:4 | tag class flag (VLAN-ish) | `rx_get_desc` -> summary +7 bits 4:3; `hi_net_rx_proc` reads in-frame words at +12/+14 when set. Exact enum [U] |
| +13 bits 6:5 | second flag class | `rx_get_desc` -> summary +7 bits 2:1; **no consumer found**; [U], possibly checksum-related |
| +16 u32 | normal path port (byte 0) / vport (byte 1) | `rx_get_desc` `[hw+16] != 0` branch; `hi_net_rx_proc` keys `g_api_priv[(port<<8)|vport]` |
| +18 u16 | normal path second summary word | `rx_get_desc` |
| +20 bits 3:0, +23 bits 7:4, +24 | no-status path: summary +2 bits / port / vport | `rx_get_desc` `[hw+16] == 0` branch |

Summary contract: 12 bytes `+0` port, `+1` vport, `+2..3` u16, `+4..5` **length**,
`+6` status, `+7` flags (bit 0 no-error, bits 2:1 class 2, bits 4:3 tag class). Recycle path:
`hi_pie_drv_rx_recv` unmaps the buffer, `skb_put`s the length, zeroes the descriptor, moves the
node to the free list and updates the "sf" pointer; `hi_pie_drv_rx_addbuf@0x3a24` allocates a
fresh skb (`size + 128`, then `skb_reserve(128)`), maps FROM_DEVICE, writes +4 and the owner
bit; `hi_pie_drv_rx_kick@0x3c5c` pushes the last produced index. [V]

### 2.2 TX normal non-TSO descriptor (v250)

Complete recovered byte image (one linear frame): `+0` u16 length; `+3` = 0x74
(valid bit 2, normal class bit 4, class selector bits 6:5 = 3 for a single linear buffer);
`+4` u32 DMA address; `+8` u32 = `(port & 0xf) | ((1 << port) << 11)` (port tag);
`+11` = 0x30 (bits 4:3 = 2, bits 6:5 = 1); `+12` = 0x20 (type-0 control bits 6:3 = 4);
`+14` = 0x01 (control bits 16:15 = 2); `+19` = 0x10 (copy of `+3` bits 4:3). Recovered from
`hi_pie_tx_set_desc@0x11ac` + type-0 helper at `0xfe8` + `hi_pie_tx_send@0x3250` +
`hi_pie_drv_xmit@0x42ac`; the request TSO branch (byte at +7 bits 6:4 = 0b010) and the type
1/2/3 control variants are decoded only at branch level and are **not** written by patch 024.
[V] bytes, [U] request metadata/hardware acceptance.

### 2.3 Ring producer/consumer semantics

- TX doorbell (`0x2008 + ch*0x10`, full 32-bit store) = `(last produced index + 1) & ring
  mask`, i.e. the ring index of the next free descriptor; completion is poll-only from the
  16-bit read pointer (`0x200c`), masked to the ring index, reclaiming everything before it.
- RX pointer word (`0x3008`) bits 15:0 = index of the **last produced** descriptor, bits
  31:16 = "sf" pointer = index of the **first outstanding** buffer (updated after every
  completion); the RX read pointer (`0x300c`) is the next descriptor to complete. All are ring
  indices [I] (fit the measured values: TX 0x639 < 4096, RX halves < 256; vendor comparison of
  a raw read pointer against a node ring index supports it).

---

## 3. What patch 024 changes

- `luofu-pie.c` / `luofu-pie.h`: `luofu_pie_rx_poll` now decodes each completion (length,
  status, port/vport), drops error/implausible/unknown-port frames with counters, delivers good
  frames with `napi_gro_receive()` (CHECKSUM_NONE, in-frame 802.1Q tag preserved) to the
  net_device that registered the completed port, and re-arms the slot with a fresh coherent
  buffer, writing the recovered RX write/"sf" pointer values. TX builds the full recovered normal
  descriptor, tags it with the MAC port and rings the masked ring index; TX reclaim uses the
  ring-index read pointer. New API: `luofu_pie_rx_register()/unregister()` and an extra `port`
  argument on `luofu_pie_xmit()`; a port table under a spinlock replaces the vendor
  `g_api_priv[]` lookup.
- `luofu-gmac.c`: reads the required `hisilicon,port` (vendor `port` value, 8..12) and refuses
  probe without it; registers/unregisters the RX port around `ndo_open/stop`; forwards TX with
  the port.
- bindings: `hisilicon,luofu-gmac.yaml` gains the required `hisilicon,port` (0..15) and updated
  description; `hisilicon,luofu-pie.yaml` description updated to the implemented decode.
- **No Kconfig symbol, no register write outside the existing arm path, no new DT node.** The
  `luofu_pie.arm=1` gate, the fail-closed probe, read-back verification and halt path are
  unchanged.

### 3.1 Cross-lane note (001-026 chain)

A first delivery of this patch used `== GRO_DROP` after `napi_gro_receive()`; 6.18 has no
`GRO_DROP` (the `gro_result_t` enum is `GRO_MERGED/GRO_MERGED_FREE/GRO_HELD/GRO_NORMAL/
GRO_CONSUMED`). The return value is not needed here, so the call is now made without a
comparison; the corrected patch sha256 is `d81a44fb7c004e66d9c686d0d38ee5cccdf52e8ba6f9e454a92f091352ae36f1`
and it compiles cleanly. The repository also carries the parallel lanes' patches `025` (LSW
egress) and `026` (bdinfo MAC nvmem); the **full `001`-`026` series applies to pristine
6.18.60 in order with 0 rejected hunks**, and the final tree (024 + 025 + 026) compiles RC=0
with 0 errors / 0 compiler warnings under `CONFIG_WERROR=y`. `026` was written against the
pre-024 GEMAC file and still applies because the 024 header edit kept the shared context line;
the applying tree, not this lane, owns the final combined file.

---

## 4. Validation (isolated clone, host-only)

Clone: `/home/shibbityshwab/openwrt-lab-pie-decode` created with
`cp -a --reflink=auto /home/shibbityshwab/openwrt-lab-net ...`; the shared clone and
`openwrt-lab-selectorfix-img` were not modified. The prepared kernel tree was removed and
`make target/linux/prepare` re-extracted 6.18.60 and applied the whole series from scratch; the
corrected patch (sha256 `d81a44fb...`) was re-prepared and recompiled from a clean extraction.

| gate | result |
| --- | --- |
| pristine 6.18.60 chain apply 001-024 | RC=0; 24 patches, 0 failed hunks, 0 `.rej`; the only `.orig` files are the same three pre-existing fuzzy-offset backups (patches 008/021) that a 001-023 baseline apply produces |
| `make target/linux/prepare V=s` | RC=0; 1,228 `patching file` lines; patch 024 applied in order after 023; 0 `.rej`; the five patched files are byte-identical to the generated sources |
| `make target/linux/compile V=s -j16` | **RC=0**; **0 errors / 0 compiler warnings** under `CONFIG_WERROR=y` (37 pre-existing `modpost: missing MODULE_DESCRIPTION()` warnings and the known OpenWrt config-sync notice are the whole warning set); zImage 3,281,400 B sha256 `f4a736cf2e0ed220c7e99fca1590913504395a2001947b66ccc8f1986e788583` |
| objects / symbols | `luofu-pie.o`, `luofu-gmac.o` built-in and byte-identical to the generated sources; `luofu_pie_rx_register`, `luofu_pie_rx_unregister`, `luofu_pie_armed`, `luofu_pie_probed`, `luofu_pie_xmit`, `luofu_gmac_probe` and `phylink_create` in `System.map` |
| TEXT_OFFSET contract | unchanged: `-DTEXT_OFFSET=0x00608000` in the compressed `.head.o.cmd`; `_text = stext = 0xc0608000`, `swapper_pg_dir = 0xc0604000` |
| config idempotent | two consecutive canonical `make kernel_oldconfig` runs RC=0, kernel `.config` byte-identical (sha256 `4a17f17ad6a4e19a06027992c2c3442ec88d2ee2557f739c403f4bb02b4a17b6`); regenerated target `config-6.18` sha256 `d1af1b2a9411b5529ddef6d5ab1852059aca594eacb5de7f24adb0e35e87665b` is **byte-identical to the 001-023 lane's regenerated artifact** (patch 024 adds no Kconfig symbol); the committed hand-maintained file is untouched |
| `dt_binding_check` | **RC=0** for `hisilicon,luofu-gmac.yaml` (new required `hisilicon,port`) and `hisilicon,luofu-pie.yaml`; the processed schema contains both, and the check is repeated **RC=0** in the final 001-026 tree (the optional `yamllint` step is skipped - package not installed, same limitation as every previous session) |
| board DTS -> DTB | final integrated DTS (committed HEAD including the parallel 026 nvmem nodes + `hisilicon,port` on the five MAC nodes): cpp RC=0 and dtc RC=0 with **0 stderr bytes**; DTB 12,076 B sha256 `5386badebc26754e01d5e585bb2632d44583797afc49bcd947f4acb0a35c085b`; `fdtget` reads `hisilicon,port` = 8/9/10/11/12 on the five MAC nodes; `dt-validate` of the final 001-026 tree's processed schema is RC=0 with only the five pre-existing baseline diagnostics (SP804 timer, two UARTs), **none naming PIE/GEMAC** |
| `checkpatch.pl --no-tree -f` | 0 errors / 0 warnings / 0 checks on the 1,644-line PIE driver, the 35-line PIE header, the 463-line GEMAC driver and both YAMLs (119/130 lines) |
| static safety checks | no RC-misc `0x10161000` / forbidden CA `0x400392f0` / ACP cells; no `devmem`/`/dev/mem`/raw MMIO helpers; every PIE register write is inside the arm-only functions reached after the `luofu_pie_arm_gate` check; the GEMAC writes remain the measured link-up/link-down words |
| cross-lane 001-026 chain | all 26 patches (024 + parallel 025 LSW egress + 026 bdinfo MAC nvmem) apply in order with 0 rejected hunks (026 still applies on top of the 024 GEMAC edits); the combined tree compiles RC=0, 0 errors / 0 compiler warnings, zImage 3,282,656 B sha256 `346fb1f629dbd5b59308fb267d090afcf046d3ea26fba81713b2dbc9d1de77bc`; a separate **fully-clean** 001-026 run (fresh kernel extraction; `make target/linux/prepare` RC=0 with 1,233 `patching file` lines, 0 `.rej`; `make target/linux/compile` RC=0, 0 errors / 0 compiler warnings, 37 pre-existing modpost warnings, zImage 3,282,528 B sha256 `08b7674749a0c155285a89fd35fa2433c2c97ce48ee5a1773cb3541365544c67`, `_text = stext = 0xc0608000`) in `/home/shibbityshwab/openwrt-lab-pie-decode-full` repeats the check from a fresh extraction |
| `shared.patch` | applies to the current repo working tree **and** to the committed HEAD copies with `git apply --check` RC=0 (5 files); the DTS hunks use one line of context around each `reg`/`hisilicon,port` insertion so they apply with either mac-lane DTS state |

Note: the first delivery of this patch used `== GRO_DROP` after `napi_gro_receive()`; 6.18 has no
`GRO_DROP` in `enum gro_result` (`GRO_MERGED/GRO_MERGED_FREE/GRO_HELD/GRO_NORMAL/GRO_CONSUMED`).
The corrected patch makes the call without a comparison and is the sha quoted above; the compile
and chain results above are from the corrected patch.

---

## 5. Proven vs still [U]

**Proven (host-only):** the decode is a direct transcription of the vendor v250 code paths - the
RX length/status/port fields, the summary contract, the normal TX descriptor byte image and the
ring pointer arithmetic are each read or written by the named `hi_kpie.ko` function; the full
`001`-`024` series applies/prepares/compiles with WERROR and no warnings; the load contract
(TEXT_OFFSET) and the config symbol set are unchanged; the bindings and the post-shared.patch
board DTS validate; checkpatch is clean; the non-arm probe path cannot touch the engine (no
MMIO/clock/reset/IRQ/NAPI) and every new code path is reachable only after `luofu_pie.arm=1`.

**Not proven - any hardware behaviour:** no PIE register has ever been written from our side; no
RX completion or TX descriptor has ever been seen by the engine; the length/status/port bit
positions are [V] from disassembly but the values on a live frame are unobserved; the two
summary flag classes are [U] semantically (no checksum offload claimed, `CHECKSUM_NONE` stays);
the descriptor port tag and type-0 control words have never been accepted by the engine; the
LSW-side egress queue handle is still [U] (the descriptor carries the port, not the vendor's
queue word); the ring-index reading of the RX/TX pointer words is [I]; the tag-class bit's
effect on an in-frame VLAN tag is unobserved; ACP coherency is still unsettled; the vendor
fragment/TSO descriptor variants are not implemented. **No packet can be claimed to reach a
wire.**

---

## 6. Attended checks needed (before any traffic claim)

1. **RX decode validation**: arm (`luofu_pie.arm=1`, plus the LSW gates for a port), send one
   known frame (ARP/ping) to the wired port, capture the RX completion words and confirm the
   decoded length/port/status; the tag-class bit must be checked against a tagged frame.
2. **TX descriptor acceptance**: arm and send one frame; confirm the TX read pointer advances and
   the frame appears on the intended switch port (switch counters), then check the descriptor
   bytes are unchanged by the engine except the written pointer.
3. **SW checksum**: the two RX flag classes are [U]; do not adopt CHECKSUM_UNNECESSARY until a
   known-good/known-bad checksum capture distinguishes them.
4. **Port mapping**: confirm `hisilicon,port` 8..12 matches the switch port that receives TX and
   that RX completions report the same port (the vendor `port` cells are the evidence).
5. **Ring-index semantics**: send > ring-size frames to check the pointer words wrap and the
   reclaim arithmetic stays correct; a 16-bit free-running counter would disprove the model.
6. **Any wedge**: recovery is the physical RESET+TFTP card; patch 024 adds no reset path.

---

## 7. Notes for the lead (apply/commit)

- Apply: `cd opensource && git apply target-skeleton/docs/incoming/pie-decode/shared.patch`,
  then move this SUMMARY to `target-skeleton/docs/port-notes/pie/SUMMARY.md` (append to the
  patch-`020` record already there), delete the incoming directory and commit the new
  `patches-6.18/024-*.patch` with the shared-file changes.
- The shared.patch is a plain git-style diff against the current working-tree state of the six
  listed files; the DTS hunks only insert `hisilicon,port` after each MAC `reg` line, so they
  also apply to the committed HEAD version.
- Parallel lanes own patches `025` (LSW egress) and `026` (bdinfo MAC nvmem); this lane touches
  no file they own except the shared documentation files named in `shared.patch`, where a union
  of the added rows/sections is the expected conflict resolution.
- No shared repository file was edited in place during the lane; the untracked additions are the
  new patch file and this incoming directory.
