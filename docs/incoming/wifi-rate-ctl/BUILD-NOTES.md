# BUILD-NOTES - how the wifi-rate-ctl patched module is produced (host-only)

**Lane:** wifi-rate-ctl (task-179).  Nothing here touches the device.  The
.ko is local evidence and is never committed.

## Chain

    opensource/docs/incoming/wifi-h2d-send/src          (base driver tree)
      + patches/0001-rate-ctl.patch                     (this lane)
    = hi5622_wifi.ko 471,580 B
      sha256 96c541599a73a8f2ae6fa020cdfc6ce5767cbf33aa19d637a92ebd94cbd42f78
      (unstripped; the sha is build-path dependent via debug info - compare
       .text or the printed sha of your build; the artifact file is
       /tmp/wifi-rate-ctl-build/artifact/hi5622_wifi.ko, local only)

The base lane's own module identity (what its build script pins) is
407,996 B sha256 2d45e5458838f7aa511bf70e5f75c64faeb4dcc88d2711596cea4d17e040b5cb;
the rate-ctl patch only adds the new module surface and one branch in the
existing post path, so the transport/HCC/config-event behaviour is identical.

## Base pins (the tree the patch was generated against)

Base source files, sha256 (as committed at wifi-h2d-send/src):

| file | sha256 |
| --- | --- |
| h2d_send.c | 69a85acff6255d0bfc19ce0c153685063d3ab79c1affce20eeca4226f77debfb |
| h2d_send.h | a4bf833878cfef4041419b699fc36c7688f52e4be8a775dcde00ff507b4c73ca |
| h2d_send_logic.c | e295310b73c5f8f733e5b260a1b63555185bdcc1b356c2b66a01128c7516a4c7 |
| h2d_send_logic.h | d9d5c7ec03f9686d50e2e33e7a9d45bebcd3336be348c6a64049981b53da02c1 |
| hcc.c | ae4cda3ff5283fb2bbe20a4bedcadc5efd3151f26d6dcbcb5afccfd7d6cab756 |
| main.c | 86cd86b74e01fba06187673214d9fd5b78029a463316e31834dd456fb93faa32 |
| Kbuild | 8ee7c6f468a5e0f2559316f76e13356b767266de9cfc61cdbaea807c7929fa14 |

If a base file drifts, tools/build-rate-ctl.sh's patch(1) dry-run fails -
re-review the base lane before adapting the patch.

## Patch delta (0001-rate-ctl.patch, 11 files: 7 edits + 4 new)

| file | change |
| --- | --- |
| src/hi5622_rate_ctl_logic.h (new) | portable decoded contract: cfg ids, blob layout, enum/value tables, request/sync structs, decision matrix |
| src/hi5622_rate_ctl_logic.c (new) | blob builders (rate_mode / ar_dev_param / tpc_pow_lvl / tpc GET / current_ration GET / strict raw), config-sync parser, fail-closed matrix, name lookups |
| src/hi5622_rate_ctl.h (new) | kernel surface (attrs, D2H rx hook, params) |
| src/hi5622_rate_ctl.c (new) | fail-closed sysfs glue: 3 params (rate_ctl default 0, rate_ctl_fb=4, rate_ctl_arbitrary=0), 8 WO attrs, 2 RO attrs, D2H g1 id3 capture |
| h2d_send_logic.h/.c | add H2D_MSG_RATE_CFG enum/name + h2d_build_alg_cfg() (the generalised g0 id2 / key 0x0101 config event; blob len = 10 + value) |
| h2d_send.c/.h | add h2d_post_rate_cfg() and the H2D_MSG_RATE_CFG build branch; every existing post site passes the new NULL alg parameter (no behaviour change) |
| main.c | register the rate_ctl sysfs group |
| hcc.c | call hi5622_rate_ctl_rx() from hi5622_hcc_deliver() (D2H capture, read-only) |
| Kbuild | add hi5622_rate_ctl.o hi5622_rate_ctl_logic.o |

No register write is added anywhere: the only device effect is the config
event frame posted through the existing SR-node / mailbox / budget path.

## Build + host tests (reproducible)

    cd opensource/docs/incoming/wifi-rate-ctl
    ./tools/run-host-tests.sh      # decode + portable logic + frame cross-check
    ./tools/build-rate-ctl.sh      # stage + patch + host tests + kernel build

tools/build-rate-ctl.sh finds the project 6.18.60 tree (openwrt-lab-upstream-
target first, then openwrt-lab-selectorfix-img; KDIR/STAGING_DIR override),
stages a source-only copy of the base tree, applies the patch with patch(1),
runs the host tests, builds the module with the OpenWrt GCC 14.4.0 toolchain
and refuses any compiler/modpost warning or error.  It copies the .ko to
/tmp/wifi-rate-ctl-build/artifact/ (local evidence only).

Recorded result of the committed patch (see evidence/):

| gate | result |
| --- | --- |
| decode selftest (vendor module) | 41/41 PASS |
| portable logic selftest | 70/70 PASS |
| frame cross-check (patched tree) | 144/144 checks PASS |
| kernel build | 0 warnings / 0 errors |
| module | 471,580 B, sha256 96c541599a73a8f2ae6fa020cdfc6ce5767cbf33aa19d637a92ebd94cbd42f78 |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| module params | 11 (base 8 + rate_ctl, rate_ctl_fb, rate_ctl_arbitrary) |

## Deployment (Lead)

Copy the artifact over the same deploy slot the wifi-h2d-send ladder uses
(/lib/modules/HI_RELEASE/hi5622_wifi.ko), then follow FIRE-RECIPE.md.  The
module is loaded with rate_ctl=1 for the enabled rungs and with the default
rate_ctl=0 for the negative-control rung.  Never rmmod after the CPU
release; the deployment is a fresh golden boot.

## Rebuilding from a clean /tmp

    rm -rf /tmp/wifi-rate-ctl-build /tmp/wifi-rate-ctl-host
    ./tools/build-rate-ctl.sh
