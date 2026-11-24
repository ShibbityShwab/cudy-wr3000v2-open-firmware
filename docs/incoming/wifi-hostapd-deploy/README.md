# wifi-hostapd-deploy - hostapd package + zero-flash deploy for the AP-start ladder (task-125)

**Lane:** wifi-hostapd-deploy (shared task **task-125**), **host-only**, 2026-11-26.
**Status:** host build + host verification PASS. **Nothing was deployed to or
executed on the router by this lane**; the attended deploy and the AP-start
ladder are the Lead's call.

**Result:** the AP-start bench image now has a buildable, pinned **hostapd
package set** (OpenWrt hostapd-mini v2.12 + its ucode dependency delta), a
**T0 zero-flash deploy path** (apk add into the running pair-B overlay; no
flash, no selector, no reboot; rollback = apk del), and the **exact AP-start
ladder command sequence** with the corrected interface/type and VAP-base
prerequisites, read-only PASS gates and stop rules.

Headline facts:

1. The tree's hostapd (**v2.12, source 2026-08-07 831364bf**) builds for the
   bench target and packages as four `.apk` files: hostapd-mini, hostapd-common,
   ucode-mod-nl80211, ucode-mod-rtnl. Every other dependency is already in the
   bench image, and **no package carries a kernel= dependency**, so the set is
   independent of the open kmod-ABI pin (modules still load from /tmp).
2. `hostapd-mini` is the right fixture: internal TLS, no libmbedtls, and the
   positive gate only needs an **OPEN** network, which never touches the
   still-M3 add_key op.
3. **The VAP must be created as AP type**, not managed:
   `iw phy phy0 interface add wlan0 type __ap`. hostapd v2.12 always sends
   NL80211_CMD_SET_INTERFACE(AP) during driver init; our driver's
   change_virtual_intf returns -EOPNOTSUPP for a real type change, so hostapd
   logs `Could not configure driver mode / nl80211 driver initialization
   failed` and never reaches NEW_BEACON. With an AP-type VAP the same
   SET_INTERFACE returns 0 (same type) and hostapd proceeds.
4. **The mode-1 VAP base is posted by a scan, not by the interface add.** The
   provider posts g0 id1 keys 0x64/0x65 only inside `hi5622_scan_op_scan`
   (vap_start_first=1). The AP ladder therefore runs one bounded
   `iw dev wlan0 scan freq 2412` before hostapd. (This corrects the
   wifi-ap-start ladder's V1 wording; the 2026-11-26 14:00 fire confirms.)
5. **Readback arithmetic correction:** ctx0 CA 0x119758 -> BAR0 **0x7D1758**
   (CA + 0x6B8000). The 2026-11-26 13:00 ledger line "0x7D2758 =
   0x119758+0x6B8000" is off by 0x1000; the validated ctx4 cross-check
   (0x11C458 -> 0x7D4458) and the D2 sequence (0x7d45e8 = ctx4+0x190) agree
   with 0x7D1758. `tools/check-ladder.py` gates this arithmetic and a mutation
   test proves the gate fires on the stale constant.
6. HOST-VERIFIED: `deploy-hostapd.sh --check` **88/88 PASS** (pins, apk
   metadata, dependency closure, payload extraction, ELF/ABI, config validity
   with a tree-source hostapd v2.12 build, ladder consistency, static safety
   audit) and `tools/selftest.sh` **14/14 PASS** (mutation/negative controls).
   No router contact, by construction and by test.

## Quick start

Host-only (never contacts the router):

    cd opensource/docs/incoming/wifi-hostapd-deploy
    ./deploy-hostapd.sh --check --evidence build-evidence/host-check.txt
    ./tools/selftest.sh --evidence build-evidence/selftest.txt
    ./tools/check-ladder.py
    ./tools/validate-configs.sh
    ./tools/readback-pack.sh            # the BAR0 readback pack for the bench

Attended (Lead only; bench link PASS + recovery door READY required):

    ./deploy-hostapd.sh --apply    --attended --yes-really
    ./deploy-hostapd.sh --verify   --attended --yes-really
    ./deploy-hostapd.sh --rollback --attended --yes-really
    # then AP-START-LADDER.md

## Files

| path | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | full lane report: build, deploy design, ladder, findings F1-F9, limits |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact build commands, config delta, artifact sizes/sha256, tree restore proof |
| [DEPLOY.md](DEPLOY.md) | zero-flash deploy procedure, modes, device gates, rollback, /tmp fallback |
| [AP-START-LADDER.md](AP-START-LADDER.md) | the exact attended AP-start command sequence + PASS gates + stop rules |
| [recipes/ap_start_ladder.json](recipes/ap_start_ladder.json) | machine-readable ladder (steps, module params, readback pack, stop/claim rules) |
| [configs/hostapd-open.conf](configs/hostapd-open.conf) | positive fixture: 2.4 GHz ch 6 OPEN, beacon_int 100, DTIM 2 |
| [configs/hostapd-wpa2.conf](configs/hostapd-wpa2.conf) | negative fixture: WPA2-PSK/CCMP, expected to stop at add_key |
| [deploy-hostapd.sh](deploy-hostapd.sh) | host orchestrator: --check / --apply / --verify / --rollback |
| [device/hostapd-device.sh](device/hostapd-device.sh) | device-side half (preflight/install/verify/rollback), executed over the project ssh key |
| [tools/expected-packages.txt](tools/expected-packages.txt) | authoritative apk identities and installed sizes |
| [tools/expected-files.txt](tools/expected-files.txt) | installed-file sha256 manifest (8 payload files) |
| [tools/image-baseline-packages.txt](tools/image-baseline-packages.txt) | bench-image baseline package list for the host dependency-closure check |
| [tools/check-ladder.py](tools/check-ladder.py) | ladder consistency gate (addresses, iface type, hostapd flags, fixtures) |
| [tools/validate-configs.sh](tools/validate-configs.sh) | hostapd config validity gate + negative control |
| [tools/build-host-hostapd.sh](tools/build-host-hostapd.sh) | builds the x86_64 hostapd v2.12 used only for config validation |
| [tools/readback-pack.sh](tools/readback-pack.sh) | prints the read-only BAR0 readback pack |
| [tools/selftest.sh](tools/selftest.sh) | device-free lane selftest + mutation tests |
| [build-evidence/](build-evidence/) | recorded host-check, selftest and config-validation transcripts (text only) |

Cross-references: [../wifi-ap-start](../wifi-ap-start/) (AP-start RE, frames,
provider patch, original ladder), [../wifi-vap-start](../wifi-vap-start/)
(mode-1 VAP base, TABLES T18 readback convention),
[../wifi-scan-ap](../wifi-scan-ap/) (provider contract, scan wiring, hostapd
fixture origins), [../wifi-bench-tooling](../wifi-bench-tooling/) (BAR0
conventions, runner, shell templates), [../wifi-image-deploy](../wifi-image-deploy/)
(the T0 mechanism this lane's deploy mirrors), [../wifi-image-integration](../wifi-image-integration/)
(the build environment and bench image).

## Safety / hard rules honoured

Host-only: no router contact, no UART, no flash, no boot-selector write, no
reboot, no module load/unload. The deploy writes only the persistent overlay
(/usr, /etc, /lib/netifd, /lib/apk db+world, /root/hostapd-deploy); the device
half is fail-closed and every mutating mode requires `--attended
--yes-really` plus a passing bench-link check and a READY recovery door. No
credentials, vendor binaries or calibration bytes are committed here; the
`.apk` artifacts are build products and stay local (paths + sha256 only).

## Honest limits

* The deploy was **not executed on the router** by this lane; the device
  preflight is the authoritative gate for the RUNNING image and fails closed.
* Nothing here proves an AP: the ladder's claim rule is the firmware VAP
  context readback, not beacons (beaconing is not observable from the same
  radio).
* The VAP-dereference window (VAP+0x10/+0x14) is [I] until the first AP fire.
* The WAL group-2 AP carrier rung (ap_start_mode=1) is unverified and optional.
* The kmod-ABI pin is untouched by this lane; modules still load from /tmp
  until the image is rebuilt from the current tree.
