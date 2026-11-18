# bdinfo MAC provisioning lane - integration record (task-36, agent mac-provisioning)

Date: 2026-11-18. **Host-only. No device, router, UART, TFTP, flash or bench tool was
touched; nothing was booted; no MAC was read from a real unit and no MAC was applied to
a real netdev.**

This directory is the incoming package for the factory-MAC provisioning lane. It holds
the shared-file integration patch (`shared.patch`); the implementation itself lives in
`target-skeleton/target/linux/hisilicon/base-files/` and the host test in
`target-skeleton/tools/`. The implementation and test are part of the lane's delivered
files (like the kernel patches of the driver lanes), not part of the shared patch.

## 1. What was delivered

| artifact | path | role |
| --- | --- | --- |
| factory-MAC helper (new) | `target-skeleton/target/linux/hisilicon/base-files/lib/functions/luofu-mac.sh` (384 lines) | read `bdinfo+0xde00` -> validate -> LAN/WAN derivation -> random fallback; **read-only** |
| board wiring (modified) | `target-skeleton/target/linux/hisilicon/base-files/etc/board.d/02_network` | sources the helper and records LAN/WAN through `ucidef_set_interface_macaddr`; also fixes the stale `/lib/functions/board.sh` source (this release has no such file; `board_config_update`/`board_config_flush` live in `/lib/functions/uci-defaults.sh`) |
| host mocked test (new) | `target-skeleton/tools/test-mac-provisioning.sh` (executable) | 103 assertions over fake `/proc/mtd`, fake mtd devices and mocked helper seams |
| incoming shared patch | `target-skeleton/docs/incoming/mac/shared.patch` | TARGET-DESIGN open question 5, MISC-SOC-BLOCKS 1.4/1.5, target-skeleton README, SKELETON-SMOKE session record |
| this record | `target-skeleton/docs/incoming/mac/SUMMARY.md` | integration notes, validation, gates, [U]/[I] list |

No vendor binary, blob, calibration data or per-unit factory value is added to the
repository. The MAC bytes are never printed in the docs or embedded anywhere.

## 2. Evidence used (and what the implementation follows)

| fact | evidence | marker |
| --- | --- | --- |
| factory MAC = plaintext 6-byte copy at `bdinfo+0xde00` (big-endian), last non-`0xff` byte `0xde05`; same six bytes as the decrypted `mac` line | `docs/MISC-SOC-BLOCKS.md` section 1.1; local-only dump `parent:dumps/router-20261108/full/mtd6-bdinfo.bin` (values withheld) | [V] |
| vendor 2.5.24 reads the LAN MAC with `mtd_get_mac_binary bdinfo 0xde00` | `parent:rootfs-2.5.24/etc/uci-defaults/99_fixwan:41` | [V] |
| vendor 2.4.15 board.d: `lan_mac = macaddr_canonicalize $(bdinfo mac)`, `wan_mac = macaddr_add $lan_mac 1`, both applied with `ucidef_set_interface_macaddr` | `parent:rootfs-2.4.15/etc/board.d/02_network:25-29` | [V] |
| vendor 2.4.15 applies the LAN base MAC to every LAN device and base+1 to WAN, and records `ori_macaddr` so a cloned MAC is detectable | `parent:rootfs-2.4.15/etc/uci-defaults/99_fixwan:17-52` | [V] |
| the U-Boot env `ethaddr` is a locally-administered random value, **not** the factory MAC; it must not be mixed in | `docs/MISC-SOC-BLOCKS.md` section 1.2; env dumps (values withheld) | [V] |
| interface map (WAN `eth3`, LAN `eth2 eth1 eth0` on R116) is vendor-userspace context only; the target has no switch driver yet, so no interface names are invented | TARGET-DESIGN.md 8.1; MISC-SOC-BLOCKS 1.1 | [V]/[I] |

**LAN rule documented:** LAN = the factory base MAC itself (canonicalized; 2.4.15
canonicalizes the decrypted `bdinfo mac` string, 2.5.24 reads the plaintext `0xde00`
copy directly — the two are the same six bytes on the measured unit). **WAN rule:** WAN =
LAN+1 under the vendor/OpenWrt `macaddr_add` arithmetic: the first three octets (OUI) are
preserved and the last three are incremented modulo 2^24, so `...:ff:ff:ff` wraps to
`...:00:00:00` without carrying into the OUI. No other offset rule exists in the vendor
scripts.

## 3. What the implementation does

1. `luofu_mac_read_bdinfo()` reads offset `0xde00` of partition label `bdinfo`:
   - primary path: OpenWrt's `mtd_get_mac_binary bdinfo 0xde00` (the vendor 2.5.24 read
     path; mtdblock device, canonical lowercase output);
   - fallback: direct read from the mtd **character** device `/dev/mtdN` found by label in
     `/proc/mtd` (never a hardcoded index), with the partition-size bound checked. This
     also covers a preinit stage where mtdblock nodes are not up yet (mtdchar is part of
     MTD core; the target kernel has `CONFIG_DEVTMPFS_MOUNT=y`).
2. `luofu_mac_validate()` rejects, with a human-readable reason: non-MAC input; all-zero;
   all-`0xff` (erased/absent); multicast; locally-administered; OUI `00:00:00`; and the
   conservative `...:ff:ff:ff` WAN-wrap case. Those two last checks are project policy
   [I], not vendor rules: a factory copy that trips them is treated as corrupt rather than
   copied into the network config.
3. `luofu_mac_add()` derives WAN = LAN+1. It uses `/lib/functions/system.sh`'s
   `macaddr_add` when that function is available (the production board.d context) and a
   byte-identical POSIX implementation otherwise; the test asserts both agree.
4. `luofu_mac_setup()` logs the source and result on stderr and returns 0 with
   `LUOFU_LAN_MAC`/`LUOFU_WAN_MAC`; on a missing/corrupt bdinfo MAC it logs **why** and
   falls back to a generated locally-administered unicast random pair (WAN still derived
   with the same +1 rule). If even `/dev/urandom` fails, it logs an error and returns 1.
5. `luofu_mac_apply()` calls `ucidef_set_interface_macaddr lan <mac>` and
   `... wan <mac>` — the normal OpenWrt board.d path (board.json -> `/bin/config_generate`
   -> netifd device `macaddr`). **No interface map is invented**: the lane does not touch
   the placeholder interface assignment, which stays blocked on the LSW switch driver
   (TARGET-DESIGN.md 8.1). Until that map exists, `config_generate` has no device/ports to
   attach the addresses to, so they are recorded in board.json only. This is stated in the
   file header and in the incoming README section.
6. The bdinfo partition is **never written**; no `mtd write`, no partition content is
   copied or fabricated; the helper has no U-Boot env read path.

## 4. Validation

### 4.1 Host mocked tests (no clone needed)

`tools/test-mac-provisioning.sh`: **103/103 assertions pass** (bash; exit 0). Covered:

- static: `sh -n` on the helper and on `02_network`, helper mode 0644 / board.d mode
  0755, board.d source wiring (and the removal of the non-existent `board.sh` source),
  no partition-write path, no env `ethaddr` read path;
- MAC normalization (colon / dash / dot / bare 12-hex / upper case / 1-digit octets);
- every validation accept/reject class with its log reason;
- the vendor `macaddr_add` derivation, including `...:ff:ff:ff` -> `...:00:00:00`,
  OUI preservation and the full-NIC wrap, compared against a mock of system.sh's
  implementation;
- the `mtd_get_mac_binary` read path (arguments asserted), the direct `/dev/mtdN`
  fallback when that helper is absent/empty/garbage, label-based (not index-based)
  partition lookup, exact `0xde00` offset with decoy bytes at `0x0000`/`0xdd00`/
  `0xde06`;
- missing table / missing label / partition too small / missing device / short read all
  fall back with their specific log reason;
- every corrupt 6-byte factory value (zero, erased, multicast, locally-administered,
  OUI 00:00:00, WAN-wrap) falls back and names the reason;
- random fallback bit handling (LA set, multicast clear, LAN/WAN relation, wrap) and a
  failed random source leaving the MACs unset with an error;
- apply order/values through the mocked `ucidef_set_interface_macaddr` and that the fake
  bdinfo device bytes are unchanged (sha256 before/after).

### 4.2 Isolated clone image build (shared clone untouched)

    cp -a --reflink=auto /home/shibbityshwab/openwrt-lab /home/shibbityshwab/openwrt-lab-mac
    # the copy's staging/build trees are rewritten from the old absolute path to the new
    # one (known reused-clone artefact; 5,777 text files rewritten)
    # target installed from "opensource HEAD" (series 001-018) + this lane's two
    # base-files files, dts/ and the two scripts mapped as in the README section 4
    make defconfig
    make world -j16

Results:

- `make defconfig`: **RC=0**, `CONFIG_TARGET_PROFILE="DEVICE_cudy_wr3000-v2"`;
- `make world -j16`: **RC=0** (started 18:34, image written 18:44, about 10 minutes on
  16 cores in the cache-reused clone; an explicit incremental re-run also returned RC=0);
- built container:
  `bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`,
  **6,185,262 B**, sha256
  `54652e56241e26eba0e95bc7fa663950c6e69ddb834dba77beb5b1afbeea2cac`;
  `scripts/luofu-image.py check-container` (independent read-back of the built bytes,
  kernel slice/rootfs UBI/slot budgets/install-status marker) **PASS**;
- built rootfs squashfs contains `/lib/functions/luofu-mac.sh` (mode 0644) and
  `/etc/board.d/02_network` (mode 0755), byte-identical (md5) to the repository files;
  the same two files are in the staged `root-hisilicon/`;
- `sh -n` on both repository and installed copies: RC=0; `tools/test-mac-provisioning.sh`
  passes **103/103** against the repository files and **103/103** when pointed at the
  installed copies (`--helper`/`--board`);
- evidence bundle (machine-local, outside the repository):
  `/home/shibbityshwab/openwrt-lab-mac/evidence-mac-20261118/` with the world build log,
  artifact hashes, container-check output, both mocked-test logs, `sh -n` output,
  squashfs listing and clone provenance.

**Staging note:** the isolated clone was installed from `opensource` HEAD
(`5989404`, series 001-018) plus this lane's two base-files files, not from the live
working tree, because another lane's in-flight patch `019` (CRG clock tree,
task-35) and its config/DTS edits were present in the working tree at build time. This
lane's validation is against the declared 001-018 series. The shared clone
`/home/shibbityshwab/openwrt-lab` was not modified (build ran in
`/home/shibbityshwab/openwrt-lab-mac`).

## 5. What is NOT claimed / open

- **Nothing has booted or run on hardware.** The bdinfo read has never executed on a
  booted target; no MAC has been applied to a netdev; preinit access to `/dev/mtdblock6`
  or `/dev/mtd6` on this target is argued from the kernel config
  (`CONFIG_DEVTMPFS_MOUNT=y`, MTD built-in) and from the standard OpenWrt board.d flow,
  not measured.
- The addresses only become effective when the switch port map supplies the lan/wan
  device/ports entries; until then they are recorded in board.json only (current
  placeholder has no netdev).
- The random fallback means a unit with missing/corrupt bdinfo gets new MACs on a
  factory reset (board.json is generated at first boot and cached); that is the documented
  fallback, not a factory-data claim.
- The LAA rejection is conservative: a genuine factory MAC with the locally-administered
  bit set (implausible for a vendor OUI but not impossible to observe) would be treated as
  corrupt and replaced by random addresses, with the reason logged. The same applies to
  the OUI-`00:00:00` and `...:ff:ff:ff` policy checks [I].
- No env `ethaddr` is read or ever written; no partition is written; no vendor userspace
  (`bdinfo`/`libbdinfo.so`) is used or needed.
- The board.d interface map remains a placeholder [X] pending the LSW switch driver; this
  lane deliberately does not invent interface names.

## 6. Integration steps for the lead

1. Apply the shared patch from the `opensource/` root:

       cd opensource
       git apply target-skeleton/docs/incoming/mac/shared.patch

   (`shared.patch` sha256
   `ddfae0dceaec4101136bdce22cc6e15fdfe6ec6afc02281fa1cf4a78fb17c889`; base:
   `opensource` HEAD `5989404`; the patch touches
   `docs/TARGET-DESIGN.md` open question 5, `docs/MISC-SOC-BLOCKS.md` sections 1.4/1.5,
   `target-skeleton/README.md` and `docs/SKELETON-SMOKE.md`).
2. Move this `SUMMARY.md` to `target-skeleton/docs/port-notes/mac/SUMMARY.md` as the
   integration record (same convention as the other lanes).
3. The delivered files in `target-skeleton/target/linux/hisilicon/base-files/` and
   `target-skeleton/tools/test-mac-provisioning.sh` are already in place; no kernel
   patch, config delta or DTS change is part of this lane.
4. In-flight lane conflict: patch `019` (task-35, CRG clock tree) was being written to
   the working tree during this lane and is not part of this validation; rebase the doc
   hunks if its README/SKELETON-SMOKE edits land first.
