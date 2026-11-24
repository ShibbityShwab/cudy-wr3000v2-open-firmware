# wifi-beacon-static — build notes and artifact identities

Host-only lane.  Nothing was flashed, no image was built, no device was
contacted.  The `.ko` artifacts are local evidence only and are never committed.

## 1. Chain and base pin

`patches/0001-beacon-static.patch` applies on top of the wifi-stability final
combined tree (chain: base 44c6cc12 → polished d145a205 → dp-tuning TXQ
175a4617 → scan-tuning R4 713899a1 → stability hardening):

    base pin (stripped): 269,688 B sha256
      b5dfb883a0352d24d3279c73c6410d60c90318b2f17c84b9d3fa006ebb1a50b2
    + patches/0001-beacon-static.patch (7 files)
    = this lane's module (host-built, NOT fired)

The patch was generated as `diff -Nru a b` from the same staged combined tree
the stability build script uses, then dry-run applied to a fresh copy:
`patch -p1 --dry-run` clean, 7 files, 762 lines.

## 2. Host checks (no kernel tree needed)

    cd opensource/docs/incoming/wifi-beacon-static
    ./tools/build-host.sh

* strict C99 compile of the patched portable sources:
  `-std=c99 -Wall -Wextra -Werror -pedantic -O2`
* `beacon_selftest`: **65 checks, 0 failures**
  (g0/WAL channel-frame KATs, refusal matrix, legacy AP sequences unchanged,
  ordering plan + truncation, capability contract, channel bounds)
* static fail-closed gates: `beacon_static` declared without initializer
  (= 0), `beacon_vap_start = 1u`, channel builder/capability present, the AP
  path branch present, `beacon_re.py` parses.

## 3. Static-RE probes (local vendor artifacts)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/beacon_re.py --wifi-ko /tmp/bootkick/hi5622v100_wifi.ko
    $PY tools/beacon_re.py --wifi-ko /tmp/vmods/hi5622v100_wifi.ko

Both vendor host versions pass **33/33** probes (firmware table A rows, channel
worker body map, AP-config/VAP-start worker maps, firmware beacon-engine
ownership, absence of a fixed beacon template, and the host AP-start call
order incl. the channel helper first and the VAP start after the AP config).
Evidence: `evidence/beacon-re-2.5.24.txt`, `evidence/beacon-re-2.4.15.txt`.

## 4. Module compile-verify (6.18.60 tree)

    ./tools/build-module.sh

* KDIR: `/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`
* toolchain: `staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi`
* **BUILD OK — zero warnings/errors**
* unstripped: 1,002,872 B sha256
  `320ad838b9d452d1382c0d5652ad9d4784bde3e10bfb9a1417818b7ae2ae0000`
* stripped (debug): 275,304 B sha256
  `e633f847cff8f1e74706d88b8fecf6e09bca58ef4983ba2ab8f82789bef6fd01`
* vermagic: `6.18.60 SMP mod_unload ARMv7 p2v8`; depends `cfg80211`; 6
  cfg80211 imports (unchanged provider contract)
* `--strip-debug` leaves `.text` byte-identical (objcopy + cmp check)
* beacon symbols present: `haf_build_chan_frame`, `hi5622_beacon_plan`,
  `hi5622_beacon_step_name`, `vapscan_chan_cfg`; strings for the plan,
  refusals, `ap_chan` and the channel-context readback line

## 5. Safety / publication notes

* Every new frame still goes through the fail-closed `hi5622_frame_post()`
  gate; with `beacon_static=0` none of the new code executes.
* New-frame builders reject out-of-range band/channel/module/vap_id and refuse
  the whole AP start before any frame on a missing provider capability.
* No vendor binary/object/firmware/calibration bytes or secrets in tracked
  text; only derived addresses/ids/offsets and original C.  The vendor
  artifacts and `.ko` outputs stay in /tmp.
