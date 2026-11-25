# What remains attended, and the exact next commands (2026-11-29)

Host-only lane: **nothing on this page was executed on the router**.  This is
the remaining work split into "needs the bench" and "host-side, can be run
now", with the commands.  The Lead fires; this lane does not.

## A. Attended items (hardware gates - no claim may be made before these)

### A1. The one attended boot of the v4 active-DTS change (unchanged from v4)

The 013a fold moved the endpoint reset lines from driver writes to the board
DTS `reset-gpios` + `reset-assert-us`/`reset-deassert-us`, consumed by the
PCIe driver.  GP1 (2026-11-24) validated the *pulse semantics* in the old
driver form; this DTS form has **not been booted**.  Acceptance (from
[`../upstream-v4/V2-REVIEW-FORM-STATUS.md`](../upstream-v4/V2-REVIEW-FORM-STATUS.md)
section 3):

- recovery armed; 2 CPUs up; serial console; NAND/UBI mounts; no Oops/BUG/panic;
- dmesg `endpoint reset pulse on 3 board line(s): 20000 us asserted, 100000 us
  settled` from the RC0 probe;
- both PCIe root complexes train (LTSSM L0) and both Wi-Fi endpoints enumerate
  (`59e7:0005` at bus 0/dev 0/fn 0 of each domain);
- GPIO0 pins 20/21/28 settle released (high) after the pulse.

Procedure (Lead, human at the bench):

```sh
tools/bench-link.sh                     # must PASS
tools/recovery-card.sh                  # recovery door READY first
tools/fire-preflight.sh                 # delivery plan must match STATUS.md
tools/check-init-shadow.sh              # source guard
# UART capture is READ-ONLY and to an absolute path:
tools/uart-capture.sh /absolute/path/uart-v2-dts-boot.log
tools/fire.sh                           # the fire itself
```

Until that boot, GP1 stays the hardware-validated line and the v4 DTS half is
host-validated only.  **This is the only item the v2 review form itself
still needs** (the form is a host artefact; the boot gates the *DTS claim*).

### A2. Bench gates for the folded driver content (not measured anywhere)

Every folded functional write is [U] as a normal-boot behaviour; the folds
must not be described as working on hardware.  When the bench session runs,
each group needs a measurement before its commit message can drop the [U]:

| fold group | what to measure |
| --- | --- |
| GEMAC 038/041/051 | with the max-frame/static-config writes in the normal boot path: link up, wire RX/TX, no AHB hang; read back 0x100/0x1c |
| LSW 031/032/036/043/046/059 | the normal-boot init write sets: pre/post readback of every programmed word, no hang, then the wired data path (same wire test as the bench image) |
| PIE 048/053/060 | RX interrupt fires with the RXCFG word, vendor-exact completion decode fields, exception-class delivery count |
| thermal 066 | v1/hwver-3 formula vs a known temperature, enable/trim write readback; decide whether the enable gate is removed or kept |

No claim is made here that any of these was run.

## B. Host-side next commands (can be run now, no device)

### B1. Owner decisions (block, not commands)

1. **Fold pipeline**: adopt Option B per [`PIPELINE.md`](PIPELINE.md) (or A).
2. **Gate policy** (K2 in [`CONSISTENCY.md`](CONSISTENCY.md)): "no module
   parameters / normal-boot init" vs "keep the attending gate".  Affects 0027-0030
   messages and 8 overlays.
3. **Fold placement** for 031 (LSW group vs new entry), 038/041/051 (GEMAC
   target re-source), 044 (0022 is not a textual fold point).

### B2. Execute the 6 blocked folds (manual re-contextualization)

Roots are attributed in [`FOLD-EXECUTION.md`](FOLD-EXECUTION.md).  Work on a
scratch copy, never on L0:

```sh
cd opensource/docs/incoming/upstream-v2-finish
python3 tools/fold-probe.py --pristine ~/luofu-reval-20261122/pristine \
        --workdir ~/fold-probe-work          # per-hunk matrix
python3 tools/fold-sequence.py --workdir ~/fold-probe-work   # 10/16 compose
```

The six are: 041 (drop 040/030 surroundings), 046 (drop 045 dump gate),
048 (fold after 041; drop 047 dump), 053 (drop 047/052 trace),
059 (fold after 046; drop 045/054/056), 060 (fold after 053; drop 055/057).
Each must keep the functional write set and drop the diagnostic gates, then be
re-checked with the probe (100 % hunks at the new target state).

### B3. Build the v2 form (Option B mechanism)

```sh
cd opensource/upstream/series-6.18
# 1. add composition/manifest.tsv, composition/overrides/, composition/appends/
#    from artifacts/folded-entries/ + artifacts/fold-overlays/ (see PIPELINE.md)
# 2. add commit-messages/0027..0030.txt (drafts in
#    ../../docs/incoming/wifi-upstream-target/submission-series.md; update to the
#    gate decision from B1)
# 3. extend SERIES/GROUPS to 30 + the composition resolution in tools/gen-series.py
python3 tools/gen-series.py
python3 tools/gen-series.py --check        # expected: 30 patches, no drift
```

### B4. Full host acceptance re-run (v2)

```sh
cd opensource/upstream/series-6.18
tools/run-validation.sh --use-tree ~/luofu-reval-20261122/pristine /tmp/v2-l2
tools/check-release-line.sh --use-tree ~/luofu-reval-20261122/pristine /tmp/v2-l1
```

`dt_binding_check` (environment-blocked today: `dtschema` not installed):

```sh
python3 -m venv /tmp/dtschema-venv && /tmp/dtschema-venv/bin/pip install dtschema==2026.9
# in the patched pristine tree:
make ARCH=arm dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml
```

Full kernel compile (the v4 lane's reflink tree + OpenWrt GCC 14.4.0; see
[`../upstream-v4/REPORT.md`](../upstream-v4/REPORT.md) section 4.3):

```sh
make ARCH=arm CROSS_COMPILE=arm-openwrt-linux- -j16 zImage dtbs
```

Image chain: rebuild in the isolated OpenWrt main clone exactly as recorded in
`opensource/target-skeleton/README.md` / `opensource/docs/SKELETON-SMOKE.md`
(`make world`; the last chain build was 2026-11-24/25) and re-run
`tools/luofu-image.py` validation.

Regenerate DTS-0001 only if the board DTS changed (it did not in v4/v2-fold
work so far):

```sh
python3 tools/gen-dts-series-patch.py
# then re-run the L1 validator, which applies/compiles DTS-0001 and checks the
# loader hand-off contract
```

Refresh hashes/package after the form is final:

```sh
sha256sum -c validation/sha256sums.txt    # after regenerating it
# submission/MANIFEST.sha256 + submission/README.md cover-email draft
```

## C. What this lane deliberately did NOT do

No L0/review-form edit, no v2 generation, no generator change, no full
compile, no dt_binding_check, no device/bench contact, no post/push.
