# incoming/qm-decode - MAC maximum-frame init (W9, patch 038)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 5).  Prior record: patch `032`
revisions 1-3 (EQM queue-manager init + tiered verification) and patch `036` (W8 PFE base init),
both integrated.  **Status:** round 4 on hardware (image 6cb3d908, six gates) verified W8 and armed
the path, but traffic still did not move (zero PIE RX polls, no TX frame, port counters).  This
package delivers patch `038`: the recovered **MAC maximum-frame register** init (W9) - the
vendor MAC drops over-max frames in both directions and our GEMAC front end never wrote it.  No
device access in this lane; publication remains NOT READY.

Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/038-net-gmac-maxframe-init.patch` | seventh attended gate `luofu_gmac.hwinit=1`: MAC register 0x100 = (pre & ~GENMASK(13,0)) | ((mtu+4) & GENMASK(13,0)), snapshot / stable-two-reads / readback / restore-on-mismatch, re-run on `ndo_change_mtu`; GEMAC refuses to register a netdev unless W9 is armed once the accepted attended path is reached | `949d83fbc451b3ec12029ab47fbaef10bef007632fc2f81556cfe8ec194337b0` |",
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | `docs/LSW-SPEC.md` (new 7.11 W9 section, 12.3 W9/W10 rows), `docs/SKELETON-SMOKE.md` (round-4b session), `target-skeleton/README.md` (038 row) | `408c33b99de9cf81f4dedda270514249b08ccc0075bfc6afada5bf0158d3a880` |",
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy of this record | - |

## 2. Round-4 hardware result and the next candidate

Image 6cb3d908 (six gates): W7 counters correct ("91 verified on bench-proven vendor-read fields
(tables 32/35/17), 77 acknowledged with diagnostics (tables 16/34; 0 differed), 1 write-only
acknowledged (table 19)"), W8 verified ("PFE forwarding-engine base init programmed and verified
(pe+0x400=0x2 pfe_glb+0x2c=0x34 pfe_glb+0x204=0x1 opp+0x110=0x0)"), connectivity armed, GEMAC
registered, eth2 1 Gbps/Full, br-lan forwarding - **still no frame**: zero PIE RX poll lines, no
box frames on the wire, PIE TX descriptors consumed.  W8 is necessary but not sufficient.

## 3. Decode: the MAC device config gap (pinned hi_kmac.ko)

- `hi_mac_hw_mtu@0x38c` reads register **0x100**, replaces its **low 14 bits with (MTU + 4)** and
  writes the word back; the access helpers are plain base+offset MMIO
  (`hi_reg_hw_read@0x41b8` / `hi_reg_hw_write@0x41c4`).  The vendor's
  `hi_mac_change_mtu@0x428c` calls it on MTU changes.
- The MAC keeps `rx_overmax_pkt` / `tx_overmax_pkt` statistics: frames longer than this limit
  are dropped **in both directions**.  A zero/reset max-frame value would explain the observed
  "link up, nothing moves" symptom exactly - and our GEMAC driver never wrote register 0x100.
- The rest of `hi_mac_hw_config@0x258` (reg 0x14 speed config, 0x18, 0x1c, and a read-modify of
  0x40 preserving bits 8..15 with bit 24 set) overlaps the measured link words our driver already
  writes (0x40 port mode, 0x1b4 mode-change latch, 0x0 MAC enable), so W9 touches only the
  maximum-frame register.

## 4. Patch 038 (W9) implementation

- New gate `luofu_gmac.hwinit=1`: once the accepted attended path is reached (PIE armed, LSW
  data path complete), the GEMAC driver refuses to register a netdev unless W9 is armed and
  verified - the same fail-closed discipline as the switch stages.
- W9 snapshots register 0x100, requires it stable across two reads, computes the vendor formula,
  writes it, reads it back, restores the snapshot on a mismatch, and re-runs from
  `ndo_change_mtu`.  Write shape [V]; field semantics beyond "low 14 bits = frame limit" [U].

## 5. Remaining gap layers (ordered)

1. **IFC default-classification table** (`ifc+0x50000`, 16-byte entries from
   `hi_pfe_ifc_init`): decoded call shape only - each entry embeds an id from the vendor's
   software table-manager allocation (`hi_tab_mgr_alloc_hash`), so that allocator's id sequence
   must be decoded before the payloads can be reproduced;
2. L2/VLAN tables + the CFE/rootif contract (`hi_kcfe_pub`, `hi_kcara`, `hi_swa`);
3. remaining MAC config words (0x14/0x18/0x1c, 0x40 read-modify), NAPT/MCC init, table 38 / VP
   class, and the PIE per-port queue handle.

## 6. Validation (isolated clone under build/linux-kbuild/waves/2026-11-20-r5-mac/, no device)

- **Series apply (001-038):** 38/38 applied, 0 failed hunks, 0 .rej/.orig; `luofu-gmac.c`
  byte-identical to the lane tree (`logs/10-series-apply-001-038.log`, sha256
  `528caad2592f3b2f6d7c04c91a44ef5fe61c224d8e068cbe446b7b8dff424f2a`).
- **Isolated clone prepare/compile:** reflink clone with patches 037+038 added (shared clone and
  `openwrt-lab-selectorfix-img` untouched): `make target/linux/prepare` **RC=0** (log sha256
  `6fedc8442384bb29ee9001af084602a68ef86061a3ec77a7c29bfb1f9a99be27`) and
  `make target/linux/compile` **RC=0** in 668.6 s (log sha256
  `b741f72680e4081a67add27b113af5c6b829bc92e3a4dcbf0b95f9f501bdedd5`) with `CONFIG_WERROR=y`
  and **zero `warning:`/`error:` lines in the whole log**; `luofu-gmac.o` 56,632 B with W9;
  `vmlinux` 57,127,888 B, sha256
  `1c62c96824f218d0ed527c96fada28f92fb09dcefea7d7e09cd002028c81eaca`; the prepared tree is
  byte-identical to the lane work tree (W9 included).
- **checkpatch:** 0 errors, 0 checks, only the expected "missing commit description" warning
  (`logs/11-checkpatch-038.log`).
- **DTS/binding/config:** patch 038 changes no devicetree, binding or Kconfig/config symbol.

## 7. Proof vs unverified

**Proven on hardware (rounds 3-4):** the W3-W8 write set arms the software data path (readiness,
GEMAC registration, 1 Gbps link, br-lan forwarding); the tiered W7 verification and the W8 PFE base
init verify on silicon; the fail-closed design refuses without the full chain.
**Unverified:** whether the MAC maximum-frame register reopens the wire (next run); every layer in
section 5; RX delivery.  A W9 boot still means "the vendor MAC init write was accepted and
verified", not "traffic flows".

## 8. Next attended step

Recovery-armed boot with the full seven-gate chain including `luofu_gmac.hwinit=1`: expect
W3 -> W4 -> W5 -> W6 -> W7 (corrected counters) -> W8 PFE verified -> connectivity armed -> GEMAC
registration -> **W9 "MAC maximum-frame register programmed and verified (0x100 = 0x5e0, mtu 1500 +
4, snapshot ...)"** -> eth2 up -> br-lan forwarding.  Then ping from the host while watching the PIE
RX poll trace and the wire: an RX poll line or a wire frame proves the corresponding direction
reopened.  If nothing moves, work the section-5 gap list (IFC table requires its allocator sequence
decoded first).

## 9. Integration notes

- The kernel patch is in the skeleton's `patches-6.18/` (038); it applies after 037 and only
  touches `luofu-gmac.c`.
- `shared.patch` is diffed against the current `opensource/` HEAD (which contains the W8/PFE
  integration, commit 1796754) and applies cleanly.
- Bench console evidence: `build/uart-logs/20261120T030815Z-uart-pfe-boot.log`.
- No vendor binary, device artifact or per-unit value is in this package.
