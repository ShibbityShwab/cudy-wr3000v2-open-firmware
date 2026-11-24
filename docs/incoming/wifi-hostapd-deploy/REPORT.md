# wifi-hostapd-deploy - REPORT (task-125)

**Lane:** wifi-hostapd-deploy (shared task **task-125**), **host-only**,
2026-11-26.  **No frame, package install or command was executed on the bench
router by this lane; the router was never contacted.**  The attended deploy +
AP-start ladder are the Lead's call.

**Scope:** the AP-start sequence is fully reversed (wifi-ap-start, task-121),
but its trigger is hostapd's NL80211_CMD_NEW_BEACON -> cfg80211 start_ap ->
provider AP sequence, and the bench image had no hostapd.  This lane delivers
(1) the hostapd package set as installable `.apk` artifacts, (2) a zero-flash
deploy path in the same T0 style as the iw/iwinfo deploy, and (3) the exact
AP-start bench ladder with the corrected prerequisites and READ-ONLY gates.

Deliverable index: [README.md](README.md).  Reproduction commands:
[BUILD-NOTES.md](BUILD-NOTES.md), [DEPLOY.md](DEPLOY.md),
[AP-START-LADDER.md](AP-START-LADDER.md).

---

## TL;DR

| item | result |
| --- | --- |
| hostapd build | OpenWrt tree HEAD `27663a0`, target hisilicon/luofu, arm_cortex-a9 musl; hostapd **v2.12** (source 2026-08-07, commit 831364bf) variant **hostapd-mini**; package build rc=0, zero warnings; target `.apk` set of 4 packages |
| package set | hostapd-mini 283,790 B, hostapd-common 1,922 B, ucode-mod-nl80211 24,228 B, ucode-mod-rtnl 29,292 B (sha256 in [tools/expected-packages.txt](tools/expected-packages.txt)); **no kernel= dependency** |
| ABI | hostapd ELF32 ARM EABI5 soft-float, PIE, interpreter `/lib/ld-musl-arm.so.1`; 8 DT_NEEDED libs all provided by baseline image packages |
| deploy path | T0 zero-flash `apk add` into the running overlay; no flash/selector/reboot; rollback `apk del`; device side fail-closed and attended-gated |
| ladder | exact sequence in [AP-START-LADDER.md](AP-START-LADDER.md) + [recipes/ap_start_ladder.json](recipes/ap_start_ladder.json) |
| host verification | `deploy-hostapd.sh --check` **88/88 PASS**; `tools/selftest.sh` **14/14 PASS**; `check-ladder.py` **107/107 checks**; config fixtures parse with tree-source hostapd v2.12 (+ negative control) |
| tree state | `.config` restored byte-identically (sha256 9052d5e3...), kernel `.config` untouched (08b6e595...), no image/kernel rebuild, no other lane artifact disturbed |

---

## 1. Pins, scope and safety boundary

Local artifacts read in place and never copied into the repository: the
OpenWrt release-line tree `/home/shibbityshwab/openwrt-lab-selectorfix-img`
and its prefetched hostapd source tarball.  Build products are local evidence
(`/home/shibbityshwab/hostapd-lane-artifacts/`); only sizes, sha256 values and
derived facts are committed.

Hard rules honoured (see AGENTS.md): host-only lane, no device contact, no
UART, no flash write, no boot-selector write, no reboot, no module
load/unload, no vendor binaries/firmware/calibration bytes in tracked text,
no credentials.  The device-side script contains no flash/selector/devmem/
module operation; a static safety audit in the host check enforces this on the
executable lines (and a selftest proves the audit fires).

---

## 2. Why hostapd-mini, and what the package set contains (F1, F2, F6)

The tree's hostapd package builds multiple variants.  **hostapd-mini** was
chosen because:

* the positive AP-start gate is an **OPEN** 2.4 GHz network, which never issues
  add_key - the still-M3 op that the WPA2 fixture would hit;
* mini uses internal TLS (no libmbedtls/libopenssl dependency);
* it is the smallest hostapd that still provides the full nl80211 driver path
  (CONFIG_DRIVER_NL80211) and the cfg80211 start_ap trigger.

The package's dependency list (from `apk adbdump`) is: hostapd-common
(exact version), libblobmsg-json20261103, libc, libnl-tiny1, libubus20261028,
libucode20261009, libudebug, ucode, ucode-mod-fs, ucode-mod-nl80211,
ucode-mod-rtnl, ucode-mod-ubus, ucode-mod-uloop.  The bench image already
carries all of them **except** hostapd-common, ucode-mod-nl80211 and
ucode-mod-rtnl - hence the four-package deploy set.  This is forced by the
OpenWrt build itself: `CONFIG_UCODE=y CONFIG_APUP=y` links
`-lubox -lubus -lblobmsg_json -lucode -lnl-tiny -ludebug` and hostapd-common
ships the APUP ucode script `/usr/share/hostap/hostapd.uc` plus
`/etc/init.d/wpad`.  NOTE: `apk add` does **not** create an `/etc/rc.d`
autostart link (verified by a device gate); the bench image must not
autostart an AP daemon.

No package carries a `kernel=` dependency, so the set is independent of the
open kmod-ABI pin (running kernel package 6.18.60~5de0e425 vs tree
6.18.60~6f995278).  The modules continue to load from /tmp exactly as the
previous bench sessions did; the canonical kmod-ABI fix remains the
image rebuild + flash lane.

---

## 3. Build (see [BUILD-NOTES.md](BUILD-NOTES.md) for exact commands)

The lane added four `CONFIG_PACKAGE_*` selections (hostapd-mini,
hostapd-common, ucode-mod-nl80211, ucode-mod-rtnl) to the validated release
tree, ran `make defconfig`, then built the two package directories with
**`make package/<dir>/compile`** (the valid single-package target; `compile`
also produces the `.apk` via package-pack.mk's `compile: PACK_$(pkg)`), and
restored `.config` byte-identically afterwards.

Result: 4 target `.apk` files (sizes/sha256 in
[tools/expected-packages.txt](tools/expected-packages.txt)), hostapd package
build rc=0.  The tree's `.config` sha256 is back to
`9052d5e36fd30e6c59276910f2b286d5a109a5dffd03f3a8e1386aece0ed075d`; the kernel
`.config` is untouched at `08b6e595...`; no target/linux or image build was
run (the serialized world-build lane had just closed its window and its
byte-identical image is untouched).

---

## 4. Deploy path (see [DEPLOY.md](DEPLOY.md))

Same mechanism as the hardware-validated T0 userspace deploy
(../wifi-image-deploy): stage the `.apk` files + tables in /tmp on the box,
resolve the transaction with `apk add --simulate` (fail-closed before any
write), install with
`apk add --allow-untrusted --force-non-repository --no-network
--repositories-file /dev/null`, then read back the package versions, the
8 installed-file sha256 values, the world entries, the hostapd version and the
absence of an autostart link.  Rollback = `apk del` + remove
`/root/hostapd-deploy`; no reboot.

The alternative "push a static hostapd binary to /tmp" is documented as a
fallback but is **not** preferred: hostapd is dynamically linked to seven
baseline shared libraries and needs the two ucode modules at runtime, so a
/tmp push must ship the same payload files anyway - the `.apk` set is the
same bytes with a managed package db and a clean rollback.

---

## 5. The AP-start ladder, and the two corrections that make it work

Full command sequence and gates: [AP-START-LADDER.md](AP-START-LADDER.md).

* **F3 - interface type is load-bearing.** hostapd v2.12's
  `wpa_driver_nl80211_set_mode_impl()` *always* issues
  NL80211_CMD_SET_INTERFACE(AP) during driver init.  If the interface is not
  already AP type, our driver's `change_virtual_intf` returns -EOPNOTSUPP,
  hostapd tries the down/retry path, then aborts with
  `nl80211: Could not configure driver mode` /
  `nl80211 driver initialization failed` - **start_ap is never called**.  The
  ladder must therefore create the VAP with `iw phy phy0 interface add wlan0
  type __ap` (iw's parser maps `__ap` -> NL80211_IFTYPE_AP; `ap`/managed are
  wrong here).  With type AP, cfg80211's SET_INTERFACE dispatches to our
  change_virtual_intf with the same type, which returns 0, and hostapd
  proceeds.  This was verified on the host with a hostapd v2.12 build from
  the tree's own source: a managed `wlan0` reproduces exactly those failure
  lines, and the driver source confirms the early-success path.
* **F4 - the mode-1 VAP base is posted by a scan, not by the interface add.**
  The wifi-vap-start provider posts g0 id1 keys 0x64 (VAP add, 48 B) and 0x65
  (VAP start, 40 B) only inside `hi5622_scan_op_scan()` when
  `vap_start_first=1`.  The AP ladder therefore runs one bounded
  `iw dev wlan0 scan freq 2412` before hostapd (cfg80211 6.18 permits a scan
  on an AP-type wdev).  This corrects the wifi-ap-start ladder's V1 wording
  ("interface add -> 0x64/0x65"); the 2026-11-26 14:00 fire shows the pair
  firing on the scan request.
* **Start command:** `hostapd -B -i wlan0 -P /root/hostapd-deploy/hostapd.pid
  /root/hostapd-deploy/hostapd-open.conf`.  `-B` daemonizes only AFTER
  interface setup, so **rc=0 means the driver's start_ap returned 0** - the
  hostapd-side half of the gate.  The firmware half is the read-only evidence
  below.
* **F7 - `hostapd -v` exits 1 by design** (upstream main.c: `show_version();
  exit(1)`); the version gate matches the `hostapd v2.12` text, not the exit
  code.
* **Expected dmesg:** `cfg80211: VAP 'wlan0' added (type AP ...)`, then
  `frame post 'ap_ssid': g0 id1 total 64 B ... consumed`,
  `cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (64 B ...)`,
  `frame post 'ap_cfg': g0 id1 total 184 B ... consumed`,
  `cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B ...)`,
  `cfg80211: start_ap sequence posted to provider 'hi5622_vapscan' (2
  frame(s))`.  cfg80211_status then shows
  `frame_post: allow_frames=16 ... refused=0 timeout=0 bad=0`.
* **Readback gates (read-only):** golden page 0x877000=0x86807FEF/+4=7/+8=2/+C=0xABCD,
  state 0x7D0B60=1, mask 0x6F8008=0, staging 0x6F800C=0;
  module-table slot0 0x829458=0x00119758; ctx0+0x190 (BAR0 0x7D18E8) nonzero
  after the VAP base; ctx0+0x18C word (BAR0 0x7D18E4) changes after ap_cfg.
  The VAP pointer dereference (VAP+0x10=100 beacon interval, VAP+0x14=2 DTIM)
  is [I] until the first fire confirms the window; the non-dereference gates
  are sufficient for a first PASS.
* **F5 - readback arithmetic correction:** the 2026-11-26 13:00 ledger line
  "module-0 ctx (BAR0 0x7D2758 = 0x119758+0x6B8000)" is off by 0x1000.
  0x119758 + 0x6B8000 = **0x7D1758**.  The validated cross-check is ctx4:
  CA 0x11C458 -> BAR0 0x7D4458, and the D2 sequence reads 0x7d45e8 =
  ctx4+0x190.  The ladder documents the recomputed constant and the checker
  fails if the stale 0x7D2758 ever comes back.

Stop rules: nonzero exception mask or staging word, page/state change, slot0
deviation, refused/timeout/bad increments, any dmesg fault, hostapd rc != 0;
no re-send of start_ap or the VAP base in the same boot (power-cycle and
analyse offline).  Never write CA 0x400392F0, never post mailbox id 5, never
send group 1/4, never rmmod.

---

## 6. Host-side verification (what was actually verified, and how)

`deploy-hostapd.sh --check` (88 gates) and `tools/selftest.sh` (14 tests) run
without any device contact.  They verify:

1. **Pins**: the committed tables' sha256 match constants baked into the
   deploy script (a local edit fails closed; negative-tested).
2. **apk identity**: size + sha256 of all four artifacts (a one-byte flip is
   caught; negative-tested).
3. **Metadata**: name/version/arch/installed-size from the real host apk
   tool, and a full **dependency closure** against
   [tools/image-baseline-packages.txt](tools/image-baseline-packages.txt)
   (the reproduced bench-image package list).
4. **Payload**: `apk extract` yields exactly the 8 installed files with the
   committed sha256 (no extras, no omissions).
5. **ELF/ABI**: ELF32 ARM EABI5 soft-float PIE, musl interpreter, 8 DT_NEEDED
   libraries each mapped to an image-baseline or set package.
6. **Config validity**: an x86_64 hostapd v2.12 built from the tree's own
   source tarball parses both fixtures without errors, and a deliberately
   broken config is refused (the gate is proven non-vacuous).
7. **Ladder consistency** (check-ladder.py, 107 checks): step order, supported
   hostapd flags, `type __ap` rule, module parameter contract, BAR0
   arithmetic against the validated conventions, PASS-gate wording, stop/claim
   rules, fixture semantics.
8. **Static safety audit**: no flash/selector/devmem/module/power-cycle/UART
   operation in any executable line; script syntax; check-mode never touches
   the ssh transport; device script fails closed on a broken stage without
   attempting a package mutation; device actions refused without
   `--attended --yes-really`.

Recorded transcripts: [build-evidence/host-check.txt](build-evidence/host-check.txt),
[build-evidence/selftest.txt](build-evidence/selftest.txt),
[build-evidence/config-validation.txt](build-evidence/config-validation.txt).

---

## 7. Interface for the next bench session (task-127)

The one-command session package needs, for the AP phase:

* the deploy step: `./deploy-hostapd.sh --apply --attended --yes-really`
  (or --verify if already installed);
* the AP ladder: H0 (hostapd deploy verify) -> V1 (`type __ap` iface add) ->
  V2 (one-channel scan for the mode-1 VAP base) -> A1/A2 (hostapd start) ->
  A3 (read-only readback) with the gates and stop rules above;
* the tooling gap in wifi-bench-tooling: its protocol-sequence shell templates
  allow iftypes `managed|monitor|AP` - for iw 6.17 the AP string must be
  **`__ap`** - and there is no `hostapd` shell template yet; the ladder JSON
  here is the reference for both.

---

## 8. Limits

* Host-only lane: **no package was installed on the router and no AP was
  started**; the deploy's device preflight is authoritative for the RUNNING
  image (it checks apk v3, overlay, free space and the dependency resolution).
* An AP is NOT claimed: beaconing over the air is not observable from the same
  radio; PASS is the firmware VAP-context readback.
* VAP deref window [I]; WAL group-2 carrier rung (ap_start_mode=1) unverified.
* hostapd's later non-fatal setup warnings (e.g. optional nl80211 commands
  this driver does not implement) are expected; the gate is the driver
  start_ap sequence + the readback, not hostapd's full log.
* The kmod-ABI pin stays open; this set deliberately carries no kernel
  dependency.
