# wifi-image-polish - image/driver polish for the clean-room Hi5622 stack

**Lane:** wifi-image-polish (shared task task-135), host-only, 2026-11-27.
**Scope:** make the flashed self-contained Wi-Fi image usable as an image, not
just as a bench ladder - module parameter defaults, boot/init integration,
hardened hostapd configs, kmod/firmware packaging policy, and the one driver
change the bench asked for (gate the AP-start diagnostic stack dump).
**No device contact, no image build, no vendor bytes.**  Everything here was
produced and verified on the host against the flashed-image artifacts and the
6.18.60 kernel tree.

## Status in one paragraph

The shipped driver (hardware-validated 242,572 B sha `44c6cc12...`, installed
in the image as 231,132 B sha `51608fd5...`) works exactly as fired, but its
compiled-in defaults are the safe `fail-closed` ones and the image has no
autoload or AP policy.  This lane ships the missing layer: an explicit-insmod
loader + rc.common service + uci policy (bench autoload=0 by default), the
working parameter incantation baked into both a `modprobe.d` mirror and the
loader, hardened open/WPA2/WPA2+WPA3 hostapd fixtures with an honest driver
capability matrix, and the kmod + firmware policy (PD4 never packaged).  It
also produces the **polished module** (`mgmt_diag=0` gate + the image defaults
compiled in) and compile-verifies it by first reproducing the hardware-fired
artifact byte-for-byte in the same tree.  The polished module is **not
fired** - that needs one attended session.

## Quick start (what an operator does)

On the flashed image, with the PD4 firmware pushed manually (see
[PACKAGING.md](PACKAGING.md)):

    /etc/init.d/hi5622-wifi start      # = /usr/sbin/hi5622-load; no arguments
    # bench policy: this is the same as loading cfg80211 + the driver with the
    # packaged defaults, running the C2 ladder, and optionally starting hostapd.

At boot nothing happens by default.  Production-style boot loading is
`uci set hi5622.main.autoload=1; uci commit hi5622; /etc/init.d/hi5622-wifi enable`.
The AP is a second switch: `uci set hi5622.ap.enabled=1`.

## File map

| path | what it is |
| --- | --- |
| [REPORT.md](REPORT.md) | the full lane report: defaults, init, hostapd, packaging, verification, limits |
| [PARAMS.md](PARAMS.md) | authoritative parameter surface + image defaults + exact working incantation |
| [INIT.md](INIT.md) | boot chain, loader/service semantics, bench vs production policy, failure modes |
| [HOSTAPD.md](HOSTAPD.md) | AP configs and the driver capability matrix (open/WPA2/WPA3, HT20/HT40, channels) |
| [PACKAGING.md](PACKAGING.md) | kmod package + versioned ABI + firmware policy (PD4 never packaged) |
| [rootfs/](rootfs/) | the deployable payload (mirrors `/`): uci policy, init script, loader, hostapd configs, firmware policy |
| [patches/0001-polish-module-defaults-and-mgmt-diag.patch](patches/) | the polished-driver source delta (defaults + `mgmt_diag` gate) |
| [refs/artifact-identity.txt](refs/artifact-identity.txt) | pinned image/module/firmware hashes |
| [refs/module-params.txt](refs/module-params.txt) | 19 image params + polished `mgmt_diag` |
| [tools/build-polished-module.sh](tools/build-polished-module.sh) | compile gate: reproduces the fired artifact, then builds the polished one |
| [tools/build-hostapd-validator.sh](tools/build-hostapd-validator.sh) | hostapd v2.12 (CONFIG_SAE) for config-file validation |
| [tools/validate.py](tools/validate.py) | static cross-check of the whole payload/docs |
| [tools/selftest.sh](tools/selftest.sh) | lane gate (static + hostapd parse + loader dry-run + evidence pins) |
| [tools/install-into-image.sh](tools/install-into-image.sh) | stage `rootfs/` into an OpenWrt tree's `files/` overlay (dry-run default) |
| [build-evidence/](build-evidence/) | committed text evidence: build logs, compile identities |

## Reproduce the host checks

    cd opensource/docs/incoming/wifi-image-polish
    ./tools/selftest.sh                 # 179 checks, PASS
    ./tools/build-polished-module.sh    # heavy: 6.18.60 tree, zero warnings,
                                        # stack1 == 44c6cc12 (asserted)

## Honest limits (detail in REPORT.md section 9)

* No on-air proof: hostapd reached AP-ENABLED, but beacon transmission and
  association need an external client (external-observer task, not this lane).
* WPA2/WPA3 remain **non-functional** until the driver gets the M3
  `add_key/set_key` ops (and M4 management-frame auth for SAE); the hardened
  configs are ready, not working security.
* The polished module is host-built and compile-verified only - one attended
  fire is required before it may replace the validated 44c6cc12 artifact.
* The PD4 firmware is vendor-derived and must keep being pushed by hand; the
  image never contains it.
