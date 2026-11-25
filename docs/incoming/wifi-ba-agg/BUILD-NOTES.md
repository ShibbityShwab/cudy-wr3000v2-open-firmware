# BUILD-NOTES - how the BA skeleton is built and why the chain is pinned

HOST-ONLY: everything below runs on the workstation; nothing contacts the
router. The .ko is local evidence, never committed.

## Chain

    5 GHz ready base tree (/tmp/wifi-5ghz-ready-build; the tree of the
      57-param bench-validated module, sha256
      1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53,
      stripped 351,008 B)
      + this lane's patches/0001-ba-skeleton.patch
        = adds hi5622_ba.[ch] + one Kbuild object line, no caller
    = BA skeleton module: 58 params, debug-stripped 359,992 B,
      sha256 784608410b4dd8e4c74be9b923d62465d4c397d12e84ee2ce58710efcc33de02
      (unstripped 1,377,860 B; .text identical after debug-strip)

The build script `tools/build-ba-skeleton.sh`:

* stages a clean copy of the base tree, applies the patch (refuses conflict
  markers), builds against linux-6.18.60 with the armv7 musl cross toolchain;
* refuses any warning/error;
* debug-strips the module and asserts the strip kept `.text` identical;
* asserts the artifact contract: **58 modinfo params** including
  `ba_enable` (uint, default 0), and the module is **provably inert** - no
  object other than `hi5622_ba.o` references a `hi5622_ba_*` symbol
  (`nm -u *.o | grep hi5622_ba_` is empty), i.e. the core is compiled and
  linked but not wired into the driver yet.

The patch is a **skeleton**: it does not hook the D2H dispatcher, the mgmt
action RX or the data TX path. Those hook points are listed in `REPORT.md`
sec. 9 (they touch files other lanes also edit: `d2h.c`, `hi5622_mgmt.c`,
`hi5622_data_tx.c`), and wiring them is the next rung after the RX gate.

## Host tests (no base tree needed)

    ./tools/run-host-tests.sh

compiles `src/hi5622_ba.c` + `src/ba_selftest.c` with
`-DHI5622_BA_HOST_TEST -Wall -Wextra -Werror` and runs **183 checks**
(`evidence/host-tests.txt`): every frame/payload byte of the vendor wire
shapes, parser accepts/rejects, the state machine (peer ADDBA request ->
response frame; host ADDBA request -> response -> g0 0x0A relay ->
g0 0x0B start/end -> DELBA), the D2H ids 2/3/0x0F/0x1D handling, the
32-MPDU burst window, and the fail-closed matrix (ba_enable 0 -> -EOPNOTSUPP
with zero callbacks, >1 -> -EINVAL, missing callbacks/overflow refused).

## Static evidence (needs the local vendor artifacts)

    .provision-venv/bin/python tools/ba_decode_check.py \
        --ko /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko \
        --fw /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin

**147 checks, 0 failed** (`evidence/static-checks.txt`): every symbol
address/size, the D2H dispatch rows, the WAL config command rows, the host
frame/payload instruction anchors, and the firmware g0 table rows + handler
anchors cited in `TABLES.md`. The tool reads the artifacts in place and
prints only addresses/derived annotations - no vendor or firmware byte is
copied into the repo.

## Rebuild from a clean /tmp

    rm -rf /tmp/wifi-ba-skeleton-build /tmp/wifi-ba-skeleton-artifact
    ./tools/build-ba-skeleton.sh      # if the base tree is absent, build it with
                                      # ../wifi-5ghz-bringup/tools/build-5ghz-ready.sh
    ./tools/run-host-tests.sh

The script needs the project 6.18.60 kernel tree and OpenWrt staging
toolchain under `/home/shibbityshwab/openwrt-lab-upstream-target` (the
selectorfix image tree is the fallback, same as the other lanes).

## Artifact pin (for the fire recipe)

| item | value |
| --- | --- |
| module under test | `/tmp/wifi-ba-skeleton-artifact/hi5622_wifi.ko` (local) |
| stripped size / sha256 | 359,992 B / `784608410b4dd8e4c74be9b923d62465d4c397d12e84ee2ce58710efcc33de02` |
| params | 58 (57 base + `ba_enable=0` default) |
| inert | no external `hi5622_ba_*` reference; default behaviour unchanged |
