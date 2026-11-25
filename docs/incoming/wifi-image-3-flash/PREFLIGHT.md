# PREFLIGHT - IMAGE #3 flash preflight verification (host-only)

**Lane:** wifi-image-3-flash (task-201). Tool: [tools/preflight-image-3.sh](tools/preflight-image-3.sh);
artifact-free tests: [tools/selftest.sh](tools/selftest.sh);
procedure: [RUNBOOK.md](RUNBOOK.md) section 3 (host) and section 5.3 (fire gate).

The preflight never contacts the router, the bench link, the UART, TFTP, the network
or any device interface: it hashes local artifacts, parses text witness files the
Lead captured read-only on the stock shell, does the flash arithmetic and prints a
verdict. It is a **static gate**, not a substitute for the attended procedure.

## Usage

```
cd opensource/docs/incoming/wifi-image-3-flash

# 1. host-artifact pass (default paths under $HOME; exit 0 PASS / 1 FAIL / 2 usage)
./tools/preflight-image-3.sh

# 2. explicit paths
./tools/preflight-image-3.sh \
    --container /home/shibbityshwab/wifi-image-3-artifacts/image-3-sysupgrade.bin \
    --artifacts /home/shibbityshwab/wifi-image-3-artifacts \
    --module-artifacts /home/shibbityshwab/wifi-image-3-module-artifacts

# 3. the fire gate (host artifacts + the stock pair-A witness + /proc/mtd)
./tools/preflight-image-3.sh --strict \
    --stock-witness /tmp/image3-stock-witness.txt \
    --proc-mtd /tmp/image3-proc-mtd.txt \
    --path rootfs-only        # or: --path container

# other modes
./tools/preflight-image-3.sh --print-pins    # print the pinned identities
./tools/preflight-image-3.sh --unit-test     # internal parser/geometry tests
./tools/selftest.sh                          # artifact-free tests of the tooling
```

Exit codes: **0 = PASS**, **1 = FAIL (do not fire)**, **2 = usage/missing input**.
Every check prints `PASS` / `FAIL` / `WARN` / `RECORD`; the summary line is
`preflight-image-3: N passed, M failed, K warnings` followed by `RESULT: ...`.

## What it verifies

| group | check | expected (IMAGE #3) |
| --- | --- | --- |
| flash unit | container size + sha256 | 7,843,840 B `4cd85b5d...a5f53` |
| flash unit | tar member set (exactly 6: dir, CONTROL, INSTALL-STATUS.txt, fip-manifest.txt, kernel, root) | exact |
| flash unit | CONTROL board/target | `BOARD=cudy_wr3000-v2`, `TARGET=hisilicon/luofu` |
| flash unit | fip-manifest slice-size / slice-sha256 / slot-limit | 3,372,638 / `13e81214...` / 8,650,752 |
| kernel | member sha256 + size | 3,372,638 B `13e81214...` (unchanged) |
| kernel | derived kernelb slot identity (0xFF-padded to 8,650,752 B) | `1794a531...54460e` |
| rootfs | member sha256 + size | 4,456,448 B `90ae340c...df2e3` |
| rootfs | UBI# EC magic; whole 128 KiB PEBs | present / 34 PEBs |
| geometry | kernelb/mtd12 size vs padded slot | 8,650,752 B, exact fit |
| geometry | rootfsb/mtd14 size vs rootfs UBI | 24,379,392 B holds 4,456,448 B; **19,922,944 B free** |
| total | container size record | 7,843,840 B (kernel + root + 14,754 B tar metadata) |
| siblings | the artifact dir (kernel-slice.bin, rootfs.ubi, root.squashfs, cfg80211.ko, hostapd, module installed) + `sha256sum -c SHA256SUMS` | pinned sizes/shas |
| module | activity + installed forms | 379,092 B `545192df...` / 358,544 B `a717c86d...` (64 params) |
| witness | stock cmdline / UBI attachment / selector / env / kernel slots (strict) | rootfsa / mtd13 / 0x10 / a / kernelb `1794a531...` |
| witness | /proc/mtd geometry (strict) | mtd11/12 `00840000`, mtd13/14 `01740000`, erase `00020000` |

The sibling-artifact checks are a convenience duplicate of the container pins; a
missing sibling directory is a WARN in the host pass (the container pins stay
authoritative) and a WARN in strict mode, never a silent pass of a wrong container.

## The two witness files (read-only on the stock shell)

Produced in [RUNBOOK.md](RUNBOOK.md) section 5.2. Format: one `key value` line each:

```
cmdline   <cat /proc/cmdline>                  (the whole rest of the line is the value)
ubimtd    <cat /sys/class/ubi/ubi0/mtd_num>
cell      <cat /sys/devices/platform/sysenv/boot_reg>
bootflag  <fw_printenv bootflag>               ("bootflag=" is stripped by the parser)
kernelb   <dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum>
kernela   <dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum>
```

For non-cmdline keys the parser uses the first token, so a raw `sha256sum` output
(a 64-hex line with a trailing `  -`) parses directly. Aliases accepted:
`boot_reg` -> cell, `mtd12` -> kernelb, `mtd11` -> kernela.
The /proc/mtd file is a plain `cat /proc/mtd` capture.

## The path decision (what --path means)

* `--path rootfs-only` (default, recommended): the strict gate **requires** the
  witness `kernelb` to equal the validated slot `1794a531...`; otherwise it
  FAILs and tells you to use the container path or recover the slot. This is the
  minimal write (mtd14 only, kernelb untouched).
* `--path container`: the kernelb identity is not a precondition because the
  container path **writes and verifies** kernelb; a differing kernelb is a WARN.
  The stock pair-A consistency checks (cmdline/UBI/cell/bootflag) still apply.

## What it does NOT verify (and where those checks live)

* The **rootfs contents** (524 files, the module inside the image, payload byte
  identity, fail-closed uci values): verified from the built bytes by the image
  lane's verifier - [../wifi-image-3/VERIFY.txt](../wifi-image-3/VERIFY.txt)
  (129/129). The preflight must not (and does not) unpack a 4.4 MB UBI/squashfs.
* The **device state**: whether stock is really booted on pair A, whether the
  recovery door is armed, whether a human is at the bench, whether the UART capture
  runs. The witness files capture the read-only device state; the human/recovery
  parts are RUNBOOK.md section 2 and are re-checked by the installer in Path B.
* The **write itself**: the preflight is read-only by construction.

## Evidence of the tooling

* [build-evidence/preflight-run.txt](build-evidence/preflight-run.txt) - the real
  host-artifact run against the baked artifacts: **23 passed, 0 failed, 0 warnings**;
  container/members/kernel-slot/rootfs/geometry/siblings/module all pinned.
* [build-evidence/selftest.txt](build-evidence/selftest.txt) - artifact-free tests:
  syntax, help/usage exit codes, wrong-container and non-tar failure paths, the
  synthetic 6-member container checks, parser unit tests, docs/script pin
  consistency, and a no-device-contact scan of the preflight script.

## Failure interpretation

| FAIL group | meaning | action |
| --- | --- | --- |
| container size/sha | this is not the baked IMAGE #3 artifact | re-obtain the artifact; never flash the wrong build |
| members / CONTROL / fip-manifest | container malformed or mixed | re-build/re-copy; STOP |
| kernel slice or slot | kernel member changed | STOP (the kernel must be UNCHANGED) |
| rootfs UBI / PEBs | root member wrong | STOP |
| geometry | partition table / artifact mismatch | STOP; re-check the on-box /proc/mtd |
| witness | stock is not a consistent pair A, or kernelb is not the validated slot | fix the pair first / use Path B |
| artifact siblings | local artifact set drifted | re-copy from the bake dir; treat the container as authoritative only after the pins pass again |
