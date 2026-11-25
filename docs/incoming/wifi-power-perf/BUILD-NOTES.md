# BUILD-NOTES - wifi-power-perf safe-subset module (host-only)

## Base (pinned)

The patch targets the **FINAL association module** work tree (the same one
that produced the 344,372 B stripped module sha256 `ab9e18a4...`): base
`all-patch` 5b587949 + wpa3-sae + ap-mgmt + sta-ops + data-tx, 56 module
params, sources with `hi5622_data_tx.c` present.  The build script refuses
any base whose `module_param_named` count is not 56 or that lacks
`hi5622_data_tx.c`.

When /tmp has been cleaned, rebuild the base with the tracked tooling:

```sh
opensource/docs/incoming/wifi-stability/tools/build-association-module.sh
# tree: /tmp/wifi-assoc-build/integ  (56 params)
```

Then:

```sh
cd opensource/docs/incoming/wifi-power-perf
BASE_TREE=/tmp/wifi-assoc-build/integ ./tools/build-patched-module.sh
```

## Patch delta (0001)

| file | change |
| --- | --- |
| `hi5622_pm_perf_logic.h` (new) | portable pure logic: PM gate, ack clamp, poll governor, ASPM decode/clear, TX window + air model, scan and beacon models |
| `hcc.c` | `pm_enable` + `pm_sleep_ack_ms` params; `hcc_pm_sleep` WO attr (gated one-shot H2D g0 id1); `hcc_pm_status` RO attr (gate inputs, D2H id4/5 counters, ASPM parity); `aspm_disable` WO attr; init from the params instead of hard-coded PM off |
| `service.c` | `hcc_poll_idle_ms` param + adaptive cadence in the service loop (fast until ACTIVE, then 20 ms after 25 quiet passes; activity resets) |
| `hi5622_data_tx.c` | `data_tx_poll_ms` param (clamped 1..100) replacing the hard-coded 5 ms completion rearm |
| `main.c` | read-only PCIe LNKCTL sample at probe + ASPM parity log (probe keeps its no-device-write invariant) |
| `hi5622_driver.h` | `aspm_lnkctl` / `aspm_read` / `aspm_cleared` fields |

No Kbuild change is needed (header-only logic).  No behavioural change at
defaults except the idle poll back-off, which is inert during traffic and in
the pre-ACTIVE boot phase.

## Build identity (host cross-build, GPL-2.0, local artifact only)

| item | value |
| --- | --- |
| base params | 56 |
| patched params | **60** (`pm_enable`, `pm_sleep_ack_ms`, `hcc_poll_idle_ms`, `data_tx_poll_ms`) |
| compiler | arm-openwrt-linux-muslgnueabi-gcc 14.4.0, kernel 6.18.60 |
| warnings / errors / undefined | 0 / 0 / 0 |
| stripped size | **349,892 B** |
| stripped sha256 | `0143c6370695925d479f07d5511231521503130d088c1c41af46c90ca65a5b9e` |
| run-to-run variance | the 20-byte build-id only (second build `a937e3d7...` same size); compare `.text` when pinning |
| artifact location | `/tmp/wifi-power-perf-build/artifact/hi5622_wifi.stripped.ko` (never committed) |

## Base variants (both verified)

Two valid base trees exist because the final association work tree also
carries lane-local fixes on top of the tracked patch chain.  The patch was
generated against the FINAL tree and applies cleanly to both:

| base | base identity | patched build (0 warnings) |
| --- | --- | --- |
| `/tmp/sibling-mgmt-fix` (FINAL association tree; delivered identity) | stripped 344,372 B sha256 `ab9e18a4...`, 56 params | stripped **349,892 B** sha256 `0143c637...` (`evidence/build.txt`) |
| tracked rebuild `/tmp/wifi-assoc-build/integ` (`wifi-stability/tools/build-association-module.sh`) | stripped 344,472 B sha256 `251d08f0...`, 56 params | stripped **349,996 B** sha256 `0d3b8a1f...` (`evidence/build-tracked-base.txt`) |

The 100 B base difference and the 104 B module difference are the lane-local
fixes in the final tree; both bases expose the same 60 params and the same
attribute surface.  `build-patched-module.sh` defaults to the FINAL tree when
present and gates the 56-param identity; set `BASE_TREE` for the tracked
rebuild.

**Not fired, not in any image.**  The Lead decides whether it is baked.

## Applying by hand

```sh
cp -a /tmp/sibling-mgmt-fix /tmp/wpp-integ
( cd /tmp/wpp-integ && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order )
( cd /tmp/wpp-integ && patch -p1 < .../patches/0001-power-perf-safe-subset.patch )
# then make -C $KDIR M=/tmp/wpp-integ modules (see build-patched-module.sh)
```
