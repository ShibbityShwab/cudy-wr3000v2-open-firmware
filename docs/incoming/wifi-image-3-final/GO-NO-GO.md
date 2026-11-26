# IMAGE #3 - FINAL GO/NO-GO (task-218, 2026-11-26)

**Lane:** image-flash-verify (task-218).  **HOST-ONLY**: no router, bench link,
UART, TFTP or network contact; nothing was written or flashed.  **The Lead
fires, attended.**  Verified inputs: the real baked artifacts under
`/home/shibbityshwab/wifi-image-3-artifacts/` and
`/home/shibbityshwab/wifi-image-3-module-artifacts/` (local, never committed).

---

## 1. Verdict

# GO - conditional.

There is **no host-side blocker**.  The IMAGE #3 flash unit is the pinned,
host-verified build; the preflight and its fire-gate logic work on the real
artifacts; every rollback artifact and the recovery image re-verify; and the
RUNBOOK's R1-R8 receipts agree with the current bench facts apart from three
documented corrections (R5 staging action, R7 receipt enrichment, R8 stale
wording - section 4).

The GO is conditional on the **fire-time gates only the bench can satisfy**
(section 5): the live strict gate, the recovery door, the human, the read-only
UART capture, and the pre-flash vendor-firmware rescue.  If any NO-GO trigger in
section 6 appears at the bench: **do not write**, record, stop.

**The image ships fail-closed** (`autoload=0`, no AP, no driver load, all new
feature gates 0).  A successful flash proves the install; it does **not** by
itself prove the 5 GHz beacon (still firmware-internal, section 4.8) and it is
the **first boot of this rootfs** (section 7).

---

## 2. Artifact verification (independent of the tool; from the shipped bytes)

Raw receipts: [build-evidence/artifact-verification.txt](build-evidence/artifact-verification.txt).
Method: `sha256sum`, `wc -c`, `tar -tvf`, `sha256sum -c SHA256SUMS`, a
0xFF-padded slot hash, `modinfo`, `readelf`, and the OpenWrt ARM objcopy for
`.text`.  All values below **PASS** unless marked.

| # | item | pinned | independently re-derived |
| --- | --- | --- | --- |
| 1 | sysupgrade container | 7,843,840 B `4cd85b5d2482...a5f53` | size + sha256 match; exactly 6 tar members (dir, CONTROL, INSTALL-STATUS.txt, fip-manifest.txt, kernel, root) |
| 2 | CONTROL / fip-manifest / INSTALL-STATUS | `BOARD=cudy_wr3000-v2`, `TARGET=hisilicon/luofu`, slice 3,372,638/`13e81214`, slot-limit 8,650,752 | all greps match; the NOT-FLASHABLE-UNTIL-INSTALLED warning is present |
| 3 | kernel slice (member) | 3,372,638 B `13e812144576...ad7f` | size + sha256 match |
| 4 | kernelb slot (0xFF-padded) | 8,650,752 B `1794a531ed19...460e` | re-derived from the member with `head -c 5278114 /dev/zero | tr '\0' '\377'` |
| 5 | rootfs UBI (member) | 4,456,448 B `90ae340cdd2d...f2e3` | size + sha match; starts `UBI#`; 34 x 128 KiB PEBs |
| 6 | root squashfs (UBI vol0) | 4,009,984 B `ecbba4f97300...bfaf` | size + sha match |
| 7 | driver module installed | 358,544 B `a717c86db2b9...1fed` | sha match; **64 params**; vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; depends `cfg80211`; license GPL |
| 8 | driver module activity | 379,092 B `545192dfdd91...3b99` | sha match; 64 params; **`.text` byte-identical to the installed form** (160,592 B, sha `494ccd42...f2a1`) |
| 9 | cfg80211.ko | 2,317,952 B `bc4d9c73f5da...6b91` | sha match; **0 pkcs7/x509 symbol refs** (the unsigned-regdb build) |
| 10 | hostapd | 1,618,388 B `fef44f3bdfc8...38e6` | sha match; version string `2.12`; `ieee80211ax=%d` present |
| 11 | mtd geometry / free space | mtd11/12 8,650,752 B, mtd13/14 24,379,392 B, erase 131,072 B; **mtd14 free 19,922,944 B** | arithmetic re-derived (24,379,392 - 4,456,448); kernelb slot fills mtd12 exactly |
| 12 | sibling artifact set | `SHA256SUMS` over 7 files | `sha256sum -c` = OK for all 7 |
| 13 | repo work copy of the same bake | as #1/#3/#5 | hashes identical to the /home artifact set |
| 14 | image rootfs content identity | 129/129 (image-3 lane) | cross-checked pins: the module, cfg80211, hostapd, iw 6.17-r1, hostapd-mbedtls, kernel package `6.18.60~6f995278...-r1`, `regulatory.db` 6,332 B |
| 15 | rollback rootfs #1 (image #2 root member) | 3,801,088 B `31e7a39c2d7b...035d` | extracted from `final-image-2-sysupgrade.bin` (`8ba1ad22`, 7,188,799 B) - match |
| 16 | rollback rootfs #2 (wifi-image-update base) | 4,456,448 B `d3c665f16e76...2d57` | sha match |
| 17 | recovery image | 22,024,351 B md5 `bc405809...f687` | `tftp/recovery.bin` matches (the pin `recovery-card.sh` enforces) |
| 18 | R5 PD4 firmware | 928,920 B `cfe24bfb1948...37b1` | **rebuilt offline** from the pinned vendor `FIRMWARE.bin` (`7fc87e20`, 928,920 B) with the repo builder - byte-identical |
| 19 | R5 wifi_block | 2,472 B `0b4ef9188fc1...45b32` | **NOT on the host** (`/tmp` was cleaned) - see condition C4 |

The flash unit's `kernel` member is the SAME slice every image #1/#2 and the
ABI image carry, so a rootfs-only write leaves kernelb untouched; the derived
slot identity is what the strict gate checks on the box.

---

## 3. Tooling verification (real runs + fire-gate rehearsal)

Raw receipts in [build-evidence/](build-evidence/).  All commands were run from
`opensource/docs/incoming/wifi-image-3-flash/tools/`.

| run | command | result |
| --- | --- | --- |
| pins | `./preflight-image-3.sh --print-pins` | exit 0; every pin matches section 2 |
| parser/geometry unit tests | `./preflight-image-3.sh --unit-test` | **5 passed, 0 failed** |
| host-artifact pass (the real gate before the fire) | `./preflight-image-3.sh` | **23 passed, 0 failed, 0 warnings**, exit 0; verdict `PASS - host artifacts are the pinned IMAGE #3 build` |
| artifact-free tool tests | `./selftest.sh` | **37 passed, 0 failed**, exit 0 |
| strict gate, ROOTFS-ONLY **rehearsal** | `./preflight-image-3.sh --strict --stock-witness /tmp/wif3-gate/witness-expected.txt --proc-mtd /tmp/wif3-gate/proc-mtd.txt --path rootfs-only` | **30 passed, 0 failed**, `RESULT: PASS - fire-ready for the ROOTFS-ONLY path` |
| strict gate, FULL CONTAINER **rehearsal** | same with `--path container` | **30 passed, 0 failed**, `RESULT: PASS - fire-ready for the FULL CONTAINER path` |
| negative 1 - kernelb not the validated slot | strict rootfs-only with kernelb `dffd16fc...` | **FAIL (exit 1)**: "kernelb/mtd12 is not the validated slot - the rootfs-only path is NOT allowed" |
| negative 2 - stock boot is not pair A | strict with cmdline `ubi.mtd=rootfsb` / ubimtd 14 | **FAIL (exit 1)**: both the cmdline and the UBI-attachment checks fire |
| negative 3 - wrong mtd14 size | strict with mtd14 `0x01000000` | **FAIL (exit 1)**: mtd14 too small for the UBI |
| path rule | strict `--path container` with a mismatched kernelb | **29 passed / 1 WARN / PASS**: container path writes+verifies kernelb; correct by design |

**Provenance caveat (important):** the positive strict runs are a **rehearsal**.
They use the RUNBOOK section-5 *expected* stock pair-A witness (hand-written) and
the **historical** `/proc/mtd` capture from `dumps/router-20261108/full/proc_mtd.txt`
(same geometry: mtd11/12 `00840000`, mtd13/14 `01740000`, erase `00020000`).
They prove the gate logic and the expected values; **the real fire gate must be
run with the witness captured live on the stock pair-A shell at flash time**
([FLASH-SEQUENCE.md](FLASH-SEQUENCE.md) section 3).  The rehearsal deliberately
does **not** claim the live box state.

---

## 4. RUNBOOK R1-R8 review vs the current bench facts

Cross-check receipts: [build-evidence/r1-r8-crosscheck.txt](build-evidence/r1-r8-crosscheck.txt).
Bench facts referenced: `STATUS.md` section 3n; the final 5 GHz entries in
`mem-entries.md` (the R5 fire, the R6a SMAC/TBTT verdict and the "do-everything"
swarm); [../wifi-ax-he-caps/FIRE-RECIPE.md](../wifi-ax-he-caps/FIRE-RECIPE.md);
[../wifi-fw-dfs-worker/FIRE-RECIPE.md](../wifi-fw-dfs-worker/FIRE-RECIPE.md);
[../wifi-tbtt-timer/README.md](../wifi-tbtt-timer/README.md).

| receipt | RUNBOOK expectation | verified vs artifacts / current facts | verdict |
| --- | --- | --- | --- |
| **R1** kernel pin / kernelb / pair | `uname -r` 6.18.60; cmdline `ubi.mtd=rootfsb` + `luofu_switch`; kernel apk `6.18.60~6f995278...-r1`; kernelb `1794a531`; selector `current_pair b` / `raw 0x00000011` | kernel package pin is in the image apk db; the unchanged kernel slice is the one every installed image carries, so kernelb `1794a531`; `luofu_record_pair` is shipped (payload manifest `124112ad...`); the `raw` sysfs attribute exists in the shipped selector driver; the cmdline marker is the one the booted image records (ledger boot receipt) | **VALID** - live values are the strict gate's job; nothing to change |
| **R2** module + userspace hashes | `/lib/modules/6.18.60/hi5622_wifi.ko` = `a717c86d`, 358,544 B, 64 params, vermagic/depends/license; 7 feature params; `cfg80211.ko` `bc4d9c73`; `hostapd` `fef44f3b`; `hostapd -v` v2.12 | all pinned values re-derived from the artifacts; `.text` identical to the activity form; the 7 feature params exist; hostapd string 2.12 and the 11AX build markers | **VALID**.  Note: the ongoing research fires load newer **per-fire** modules from `/tmp` (e.g. the 66-param BA-TX module, the TWT module); those do not change the shipped file hashes, and the image #3 **merged-module decision stands** (64-param `a717c86d` is the image baseline) |
| **R3** fail-closed policy | gates `band5g=0 vap5g=0 ap_chan_wait_ms=300 desc_table=0 dfs_mode=0 he_caps=0 ba_enable=0`; `autoload=0`; `ap.enabled=0`; module not loaded; `/etc/hi5622` file list | all 9 uci values match the shipped `/etc/config/hi5622`; the module is not autoloaded; the 10 `/etc/hi5622` files listed in R3 are exactly the shipped set | **VALID** |
| **R4** regdomain / 5 GHz IR | no regulatory.db/pkcs7 failure; `iw reg set US` -> `country US: DFS-FCC`; the 5170-5250 MHz IR line without NO-IR/PASSIVE-SCAN | the shipped cfg80211 is the unsigned-regdb build (0 pkcs7/x509 refs, 2,317,952 B pinned); `regulatory.db` 6,332 B shipped; `iw` 6.17-r1.  The live `iw reg get` is the receipt | **VALID** |
| **R5** vendor firmware re-push | `/lib/firmware/hi_wifi/FIRMWARE.PD4.bin` `cfe24bfb`, 928,920 B; `wifi_block.bin` `0b4ef918`, 2,472 B; both never packaged | PD4 **rebuilt byte-identically** offline from the pinned vendor `FIRMWARE.bin`; but `/tmp/hi5622-pd4` and `/tmp/blocks` are **gone** (`/tmp` cleaned) and the 2,472 B block is not on the host | **PARTIAL - actionable (C4).**  The block does not disappear by itself: it is currently on the box in `/lib/firmware/hi_wifi/` (pushed by hand); the rootfs write wipes the overlay.  **Rescue it before the flash** (or find the local 618-word capture and rebuild with `mkblocks.py`).  PD4 can always be rebuilt |
| **R6** module load / C2 / page golden | loader lines (kernel/module/params/wiphy/C2/done); `cat $D/boot $D/status` -> page `0x86807FEF` / state 1 / mask 0 + transport `fw_verified=1 ... cpu_released=1`; no exception | the loader `/usr/sbin/hi5622-load` is shipped and its expected lines are in the script; the module exposes every sysfs attribute R6 reads (`boot`, `status`, `cfg80211_status`, the ladder attrs, `h2d_status`); the page-golden contract matches the bench-validated receipts (page golden is checked **after** the boot/release; a pre-boot page check is not a valid gate) | **VALID** (live receipt on-box) |
| **R7** HE AP (2.4 GHz 802.11ax) | `hostapd -v` v2.12; `ieee80211ax=1` in `hostapd-open-ax.conf`; dmesg HE advertisement line; `iw phy phy0 info` HE Iftypes AP block: HE MAC cap byte0 0x01, PHY cap byte0 0x02, MCS/NSS rx80/tx80 `0xfffa`, no 6 GHz HE line; hostapd AP-ENABLED with no "Driver does not support HE" | the artifact receipt is consistent with the achieved bench fact: the AX-1 rung is **live on air** - `cudy-ap-test-he` beacons with the **HE Capabilities IE** and the **HE Operation IE** (`STATUS.md` 3n; `HE-IE-VERDICT|PASS` is the external checker).  The shipped `hostapd-open-ax.conf` is `ssid=cudy-ap-test-he, hw_mode=g, channel=6, ieee80211ax=1`; `iw` 6.17-r1 supports the HE Iftypes decode | **VALID, enrich the receipt:** also record the HE Operation IE (**params `0x003FF4`, BSS Color 25**) and the HE Cap/PHY values on air; keep the "no beacon claim before the rung" rule |
| **R8** 5 GHz gates + F2 RF programme | gates `band5g=1 vap5g=1 desc_table=1 dfs_mode=1 ap_chan_wait_ms=300`; `hi5622-load`; params read back `1 1 1 1`; RF receipt: mode byte `P+0xF04 = 3`, `R+0x3A8 = 0x01800060`, `R+0x3AC = 0x03E6138A`, band-1 context bound; **no 5 GHz beacon claimed** (RUNBOOK still says "the TBTT arm is the open piece, fw-tbtt-arm lane") | the gate set and the four expected RF reads are **exactly** the F2 fire recipe (the artifact's module carries the F2 chain: `desc_table`, `dfs_mode`, `band5g`, `vap5g`, `ap_chan_wait_ms`; the source chain is the F2 tree + field-B fix).  **But the RUNBOOK's last sentence is stale:** per the R6a verdict (`../wifi-tbtt-timer/README.md`), the TBTT is **firmware-internal**: no g0-id1/H2D key maps to a beacon/timer worker, the band start/MAC start write no timer word, and the live SMAC latch `0x401015CC` reads **0 - never started**.  So no host fire can close the beacon; the lane is now the firmware-side SMAC-start decode (`fw-smac-start`), not an "fw-tbtt-arm" rung | **VALID values, CORRECT the wording.**  Keep the RF receipt and the explicit "no 5 GHz beacon" rule; replace "the TBTT arm is the open piece, fw-tbtt-arm lane" with "the TBTT is firmware-internal (R6a, `wifi-tbtt-timer`); no host message arms it; the beacon is not reachable by this rung" |

**Summary of the review:** R1-R4, R6 are ready and require no edits; R7 is ready
but should carry the achieved on-air HE receipt; R8's expected values are right
but its stale TBTT lane wording must not be read as "run another host rung"; R5
has a real pre-flash action (rescue `wifi_block.bin`).  Nothing in R1-R8
contradicts the achieved features (2.4 GHz AP + 802.11ax HE + the 5 GHz gates);
none of the receipts depends on the RX gate or on a 5 GHz beacon.

---

## 5. The conditions (fire-time gates)

- **C1 - live strict gate.**  Switch to a consistent stock pair A, capture the
  two witness files read-only (RUNBOOK 5.2), then:
  `./tools/preflight-image-3.sh --strict --stock-witness ... --proc-mtd ... --path rootfs-only`
  must print `RESULT: PASS - fire-ready for the ROOTFS-ONLY path` (exit 0).
  If the live kernelb is not `1794a531...`, the rootfs-only path is **not
  allowed**; use `--path container` **and Path B** (which rewrites and verifies
  kernelb).  If any strict check fails: no write.
- **C2 - recovery + human + instrumentation.**  A human owns the power/RESET
  button; `tools/bench-link.sh` PASS; `tools/recovery-card.sh --arm` then
  READY (`recovery.bin` 22,024,351 B md5 `bc405809...`); read-only UART capture
  running (`tools/uart-capture.sh start --label image3-flash --timeout 7200`)
  with the absolute log path recorded.  No human = no fire.
- **C3 - artifact freeze.**  Do not rebuild between the preflight and the write.
  Re-run `./tools/preflight-image-3.sh` immediately before the fire; it must
  still be 23/0.  If the artifacts do not match the pins: wrong build, STOP.
- **C4 - vendor-firmware rescue (do this before the write).**  The current box
  has the two by-hand vendor artifacts in its overlay `/lib/firmware/hi_wifi/`.
  The rootfs write wipes that overlay.  Before the flash, either copy them off
  the box (`tools/rscp.sh root@<box>:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin ...`
  and `.../wifi_block.bin`) and hash them against `cfe24bfb` / `0b4ef918`, or
  find the local 618-word capture and rebuild the block with
  `../wifi-driver-clean/tools/mkblocks.py`.  PD4 is trivially reproducible.
  (If the rescue is impossible, the flash still installs, but R6-R8 rungs cannot
  run - flag it to the Lead.)
- **C5 - accept the known losses.**  `rootfs_data`/overlay and `/tmp` are wiped
  (the current `/tmp` per-fire modules and `/root/fire-*.sh` scripts are lost;
  they are host-rebuildable), the flash is the rootfs's first boot, and the box
  must come up on 6.18.60.  Pair A (stock 2.5.24) is the rollback and is never
  written by Path A.
- **C6 - post-flash sequence.**  R1-R4 first (boot identity + userspace),
  R5 before any load, then R6-R8 as deliberate single rungs on fresh boots.  No
  `rmmod`; module parameters are load-time only; `/usr/sbin/hi5622-load`
  (autoload is 0).  The image #3 module is the 64-param merged baseline; the
  ongoing 5 GHz/AX/TWT/RC/BA work keeps loading its per-fire modules from
  `/tmp` as it does today - the merged-module decision stands.

---

## 6. NO-GO triggers (do not write / do not continue)

1. Any preflight FAIL - host pass or live strict gate.
2. Recovery door not READY, TFTP down, no human, no read-only UART capture.
3. The live witness shows the stock boot is **not** a consistent pair A
   (cmdline rootfsb, UBI on mtd14, env `bootflag=b`, cell 0x11/0x21) - fix the
   pair first, reboot, restart.
4. Path A chosen while the live kernelb is not `1794a531...` - use Path B.
5. The artifact hashes do not match the pins at fire time (rebuilt/edited).
6. The box does not come up on **stock A** after the pair switch - STOP (the
   pair state is not what this runbook assumes).
7. During the write: uploaded rootfs sha mismatch; `ubiformat` rc != 0;
   kernelb changed in Path A; the active pair changed; the handover read-back is
   not `bootflag=b` + cell `0x11`.  STOP per RUNBOOK section 10 - pair A still
   boots.
8. After the write: no 6.18.60 boot within ~240 s (then a plain power-cycle;
   still silent -> physical RESET+TFTP recovery); or any R1-R4 receipt mismatch
   (wrong kernelb, missing cmdline marker, wrong module hashes, wrong apk pin) -
   roll back (ROLLBACK.md), do not enable gates.

---

## 7. Residual risks (accepted, by design)

- **First boot of this rootfs.**  IMAGE #3 is host-verified 129/129 but this
  exact rootfs+squashfs+UBI has never been booted.  Its base and each delta are
  bench-proven classes, and the image is fail-closed, but the flash *is* the
  test.  Pair A rollback + the armed recovery door are the mitigations.
- **The exact 64-param module combination is not fire-tested** (its parents
  are); every new gate ships 0, so the combination is inert until a rung
  enables it.  R6-R8 are its first tests.
- **The 5 GHz beacon is not reachable yet.**  The RF chain is programmed
  (`mode 3`, `R+0x3A8=0x01800060`, `R+0x3AC=0x03E6138A`, band-1 MAC start
  armed at `0x4AF2C`, page golden) but the beacon timer is the firmware's
  internal SMAC/TBTT latch (`0x401015CC = 0`); no H2D message arms it (R6a).
  R8 must not claim a beacon.
- **The RX association gate is still open** (the firmware-internal RX thread /
  the F1 delivery path); no client associates with our AP.  The flash does not
  change this.
- **NAND is degraded** (ECC/JFFS2 errors recorded): one write cycle per session,
  no repeated cycling.  Path A is the single `ubiformat`; Path B adds a
  redundant kernel erase/write of identical bytes.
- **Overlay size:** the 4,456,448 B UBI uses 34 of the slot's 186 PEBs;
  19,922,944 B remain for the dynamic `rootfs_data` volume (image #1's lineage
  has already proven this UBI layout boots with a writable overlay).  The stock
  DTS's separate `mtd15 rootfs_data` partition is vendor history; this image
  uses volume 1 in the rootfs UBI (the `ubinfo` check in ACCEPTANCE.md confirms).

---

## 8. What the flash delivers (current feature set at the moment of writing)

- The 2.4 GHz AP (AP-ENABLED, beaconing) and the **802.11ax (Wi-Fi 6) HE AP**
  with the HE Capabilities/Operation IEs and BSS Color 25 - both bench-proven
  classes, shipped fail-closed (they require a deliberate rung).
- The 5 GHz chain: every host message + firmware state proven (order, 0x0065 /
  0x0242, band-1 MAC start 0x4AF2C, RF programmed, beacon template, page
  golden); **only** the firmware-internal TBTT timer is open.
- BA / rate control / TWT machinery decoded and fail-closed, parked behind the
  RX gate for end-to-end; the RX H2D trace done (the gate is firmware-internal).
- It does **not** deliver a 5 GHz beacon, a client association, or the vendor
  firmware (never packaged - push by hand).  It is a bench research image, not
  a release, and it is **not** vendor-`sysupgrade`-flashable.

---

## 9. Evidence map

| receipt | file |
| --- | --- |
| real host preflight (23/0) | [build-evidence/preflight-host-artifacts.txt](build-evidence/preflight-host-artifacts.txt) |
| pins | [build-evidence/preflight-print-pins.txt](build-evidence/preflight-print-pins.txt) |
| parser/geometry unit tests (5/0) | [build-evidence/preflight-unit-test.txt](build-evidence/preflight-unit-test.txt) |
| tool selftest (37/0) | [build-evidence/selftest.txt](build-evidence/selftest.txt) |
| strict-gate rehearsal, positive (rootfs-only + container) | [build-evidence/gate-rehearsal-rootfs-only-PASS.txt](build-evidence/gate-rehearsal-rootfs-only-PASS.txt), [build-evidence/gate-rehearsal-container-PASS.txt](build-evidence/gate-rehearsal-container-PASS.txt) |
| strict-gate rehearsal, negative (3 fail-closed cases) | [build-evidence/gate-rehearsal-NEG-kernelb-fails.txt](build-evidence/gate-rehearsal-NEG-kernelb-fails.txt), [build-evidence/gate-rehearsal-NEG-rootfsb-fails.txt](build-evidence/gate-rehearsal-NEG-rootfsb-fails.txt), [build-evidence/gate-rehearsal-NEG-mtd14-fails.txt](build-evidence/gate-rehearsal-NEG-mtd14-fails.txt) |
| independent artifact verification | [build-evidence/artifact-verification.txt](build-evidence/artifact-verification.txt) |
| R1-R8 cross-check | [build-evidence/r1-r8-crosscheck.txt](build-evidence/r1-r8-crosscheck.txt) |
| preconditions inventory | [build-evidence/preconditions.txt](build-evidence/preconditions.txt) |
| the upstream runbook + tooling | [../wifi-image-3-flash/RUNBOOK.md](../wifi-image-3-flash/RUNBOOK.md), [../wifi-image-3-flash/PREFLIGHT.md](../wifi-image-3-flash/PREFLIGHT.md) |
| image identity | [../wifi-image-3/HASHES.txt](../wifi-image-3/HASHES.txt), [../wifi-image-3/VERIFY.txt](../wifi-image-3/VERIFY.txt), [../wifi-image-3/FLASH-FEASIBILITY.md](../wifi-image-3/FLASH-FEASIBILITY.md) |
