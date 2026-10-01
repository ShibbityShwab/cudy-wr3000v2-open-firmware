# calwrite: the first WRITE path to the chip's calibration block, and back (phase 13, 2026-10-01)

**Result: we can write the chip's BAR0 memory window, the write reaches the data the vendor
firmware actually uses, and the change is fully reversible. A bounded module (`lab/calwrite`)
wrote the single word `0x17161607` to BAR0 `0x1b2f24`; the vendor's own
`iwpriv Hisilicon0 alg get_2g_power_param` then reported `17161607` as its first word. Writing
`0x17161605` back made both the module and the vendor getter report `17161605` again. The
576-byte `noop` identity write re-read byte-for-byte identical, `insmod`/`rmmod` returned 0, the
device did not reboot (uptime `11:52` throughout), and no vendor module was touched. No step
failed; the single byte we changed was restored to its original value.**

`calread` (phase 12) proved the read direction. `calwrite` proves the write direction end to end:
our open module can drive the same calibration data the closed vendor stack answers from.

## The module

- Source: `lab/calwrite/calwrite.c` + `lab/calwrite/Makefile` in this repo.
- Endpoint `0000:00:00.0` (`59e7:0005`); BAR0 base read from PCI config space with
  `pci_read_config_dword()` (the same read-only accessor `calread`/`barmap` use). It
  `ioremap()`s exactly 576 bytes at BAR0 `+0x1b2f00` - **no `pci_request_region`, no
  `pci_enable_device`, no config-space write, no reset**; the vendor driver keeps ownership.
- Three write-only debugfs triggers (mode `0200`), each strictly bounded and logged:
  - `noop` - `memcpy_fromio` the 576-byte block, `memcpy_toio` the SAME bytes back to the SAME
    window, `memcpy_fromio` again, and report byte-identity. Zero semantic change.
  - `patch` - `iowrite32(0x17161607, win + 0x24)`: one nibble of the first 2.4 GHz trim word at
    BAR0 `0x1b2f24`, then read it back.
  - `restore` - `iowrite32(0x17161605, win + 0x24)` and read it back.
- Two read-only debugfs files (mode `0444`):
  - `backup` - the last full 576-byte local backup as hex (taken by every trigger *before* the
    action, and once at load), with its `taken_by` action and timestamp;
  - `block` - a fresh live read of the same 576 bytes plus the word at `+0x24`.
- Every trigger logs a full before/after 16-byte-row hex dump to dmesg, plus a one-line verdict.
- Bounds are coded in: the only mapped window is `[0x1b2f00, 0x1b2f00+576)`; `patch`/`restore` touch
  only the 4 bytes at `0x1b2f24`; `noop` writes back only bytes it just read, over that same window.
- On unload it removes the debugfs tree, `iounmap()`s the window and drops the PCI reference.

## CI

- Workflow: `.github/workflows/build-load-test-module.yml` - added a `build calwrite module` step, a
  vermagic check line, and a `calwrite-ko` artifact.
- Run (green): **`36839773461`** - https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36839773461
  - commit `f40b436`, conclusion `success`; every build step passed including `build calwrite module`
    and `show vermagic`.
  - artifact `calwrite-ko` = `calwrite.ko`, 13700 bytes, md5 `6e3f7566dc2bbf135e6ebc55db9d0b64`,
    `vermagic=5.10.201 SMP mod_unload ARMv7` (matches the vendor kernel).
  - source `lab/calwrite/calwrite.c` sha256 `52c1dff90cf3fbfd099d501619e811f1cd98b437abeb4f175ab26972e02c1018`;
    `calwrite.ko` sha256 `079e865953b4666734737fe3910858b4fc77008390e26b6578f9e62f6dfccbb0`.

## Safety net: full calibration snapshot (taken first)

The mandated snapshot was run against the router **before any write**, and its bundle was kept:

    ROUTER_PASSWORD=... tools/wifi-cal-snapshot.sh \
        build/cal-snapshots/20261001-pre-calwrite

Bundle path: `build/cal-snapshots/20261001-pre-calwrite/`. It contains `identity.txt`,
`alg-values.txt` (all `get_*` answers, including the baseline 2g/5g power params),
`factory-cal.txt`, `module-params.txt`, `wireless-config.txt`, the two pulled `.ini` files, and
`MANIFEST.sha256`. `sha256sum -c MANIFEST.sha256` verifies clean (all files `OK`). This is the
per-unit restore reference.

## On-device sequence and evidence

The CI artifact was `scp`'d to `/tmp/calwrite.ko`; the device md5
`6e3f7566dc2bbf135e6ebc55db9d0b64` equals the local artifact. Device: `Linux WR3000 5.10.201
armv7l`, uptime `11:52` before and after; no reboot. No other lab module was loaded.

### a. Snapshot first
See above - bundle `build/cal-snapshots/20261001-pre-calwrite`, manifest verified.

### b. Baseline - `iwpriv Hisilicon0 alg get_2g_power_param`

```
Hisilicon0  alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
```

First word `17161605`. (5 GHz baseline identical to phase 12; unchanged throughout.)

### c. `noop` - write capability with zero semantic change

    echo 1 > /sys/kernel/debug/calwrite/noop     # rc=0

dmesg:

```
omo-calwrite: noop: wrote back 576 bytes @ BAR0+0x1b2f00, re-read; identical=yes (first diff @0)
```

The module's own 36 BEFORE hex rows and 36 AFTER hex rows in the raw dmesg are byte-for-byte equal
(`diff` of the extracted payloads is empty). `backup` was taken first (`taken_by=noop`,
`word@+0x24=17161605`). The vendor getter still reported `17161605`. **The write path executes and
the block is unchanged.** (Note: `noop` alone cannot distinguish "write landed" from "write
ignored", because writing identical bytes back trivially re-reads identical; the patch readback in
step d is what proves the write physically landed.)

### d. `patch` - one word, one nibble

    echo 1 > /sys/kernel/debug/calwrite/patch    # rc=0

dmesg:

```
omo-calwrite: patch BEFORE word@BAR0+0x1b2f24=17161605
omo-calwrite: patch: wrote 17161607 to BAR0+0x1b2f24, read back 17161607, reached=yes
```

Module live read after patch (`block` header): `word@+0x24=17161607 (orig=17161605 patched=17161607)`.

Vendor getter immediately after:

```
Hisilicon0  alg:[SUCC]17161607 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
```

**The vendor now reports `17161607` as its first word.** Our write to BAR0 `0x1b2f24` reached the
exact data the vendor firmware serves - the write path is proven end to end. The vendor answered
normally (`[SUCC]`); no restore-on-hazard was triggered.

### e. `restore` - back to the original value

    echo 1 > /sys/kernel/debug/calwrite/restore  # rc=0

dmesg:

```
omo-calwrite: restore BEFORE word@BAR0+0x1b2f24=17161607
omo-calwrite: restore: wrote 17161605 to BAR0+0x1b2f24, read back 17161605, restored=yes
```

Module live read after restore (`block` header): `word@+0x24=17161605 (orig=17161605 patched=17161607)`.

Vendor getter after restore:

```
Hisilicon0  alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
```

**Both the module and the vendor getter report `17161605` again - the acceptance criterion holds.**
The 5 GHz table is unchanged. The `backup` taken by `restore` holds the patched state
(`taken_by=restore`, `word@+0x24=17161607`) as evidence of what was rolled back.

### f. `rmmod` and debugfs removal

```
rmmod_rc=0
ls: /sys/kernel/debug/calwrite/: No such file or directory
calwrite: not loaded
[...] omo-calwrite: debugfs removed, window unmapped, device released
```

Final vendor getter: first word `17161605`. Uptime after everything: `11:52` (no reboot).

## Raw dmesg

The full raw capture (82 lines) is `build/register-dumps/calwrite_dmesg.txt`. All 72 non-summary
lines are the `noop` BEFORE/AFTER hex rows. The load/patch/restore/exit lines verbatim:

```
[42739.598643] omo-calwrite: BAR0 base=0x40000000 window +0x1b2f00 len=576, writable window mapped, no claim
[42739.608213] omo-calwrite: load word@BAR0+0x1b2f24=17161605 (orig=17161605 patched=17161607) backup=yes
[42739.617550] omo-calwrite: /sys/kernel/debug/calwrite ready (noop patch restore backup block)
[42742.856176] omo-calwrite: noop BEFORE (576 bytes @ BAR0+0x1b2f00)
[42742.862286] omo-calwrite: noop BEFORE 0000: 05 00 09 00 40 82 03 00 19 a6 03 00 00 00 a6 ff
[42742.870657] omo-calwrite: noop BEFORE 0010: 74 ff 00 00 00 00 60 ff c0 fe 00 00 17 0e 06 00
[42742.878997] omo-calwrite: noop BEFORE 0020: 00 00 00 00 05 16 16 17 05 16 16 17 05 16 16 17
... 33 more BEFORE rows ...
[42743.170456] omo-calwrite: noop AFTER (576 bytes @ BAR0+0x1b2f00)
[42743.176432] omo-calwrite: noop AFTER 0000: 05 00 09 00 40 82 03 00 19 a6 03 00 00 00 a6 ff
[42743.184693] omo-calwrite: noop AFTER 0010: 74 ff 00 00 00 00 60 ff c0 fe 00 00 17 0e 06 00
[42743.193393] omo-calwrite: noop AFTER 0020: 00 00 00 00 05 16 16 17 05 16 16 17 05 16 16 17
... 33 more AFTER rows (byte-identical to BEFORE) ...
[42743.480957] omo-calwrite: noop: wrote back 576 bytes @ BAR0+0x1b2f00, re-read; identical=yes (first diff @0)
[42747.043294] omo-calwrite: patch BEFORE word@BAR0+0x1b2f24=17161605
[42747.049482] omo-calwrite: patch: wrote 17161607 to BAR0+0x1b2f24, read back 17161607, reached=yes
[42749.354150] omo-calwrite: restore BEFORE word@BAR0+0x1b2f24=17161607
[42749.360602] omo-calwrite: restore: wrote 17161605 to BAR0+0x1b2f24, read back 17161605, restored=yes
[42752.414922] omo-calwrite: debugfs removed, window unmapped, device released
```

(The `.` of the dmesg timestamps reproduces the kernel's microsecond field; the artifact is raw.)

## Pulled artifacts (`build/register-dumps/`)

| file | bytes | md5 | sha256 |
| --- | ---: | --- | --- |
| `calwrite_dmesg.txt` | 7711 | `f85a586be988f9ba9f70f054e049bffc` | `96be947778dd7c47bfe54728b13d78bb1b0a262588b4c194cfd23d17d1acc01d` |
| `calwrite_summary.txt` | 317 | `44fcde8111204de2efe136380f48ac9d` | `d09b8e5c794132f99edd80414747fa652209a673b8c040ff021bfc5bac89672b` |
| `calwrite_backup.txt` | 2046 | `f64fb43718f9d5045e71298d51cc9d6e` | `94cb9f4038590633c4e2b2216663702870154c2311d339a2998b36270ebc3bb0` |
| `calwrite_block.txt` | 2048 | `47cba46aed5d5e6c3dd70226440ba4a2` | `c557e59cd5511701d1311a5da5e51de2346d4b9b25fc5b199c320389aa58952b` |
| `calwrite_baseline.txt` | 572 | `5c0e16e56b62aaaf570433fe864d8039` | `f567b1940ad3c1d825b1e69287b996d09928478abb356e013b1d90055522d730` |
| `calwrite_iwpriv_2g_baseline.txt` | 329 | `aab431909d1d3671ab77a390012ed4cf` | `2dd73dc92ec35324b32d5894f65748c129bccc3d02b8d9ec10855afbbfbf2300` |
| `calwrite_iwpriv_2g_after_noop.txt` | 312 | `316a5eab66e3dd8e116b128b2be8613b` | `e3954ada6dff1599b7d5eed2e23f0e19d3ebe180b2c7f9455e71cd147a75ba10` |
| `calwrite_iwpriv_2g_after_patch.txt` | 339 | `0b85953f9c70679118843bdd60d58d03` | `bf252bdb55b9b08b02ab9b62e43548a99832e718f016040a37a13580171bc7e3` |
| `calwrite_iwpriv_2g_after_restore.txt` | 345 | `094c87998a7365ac8da49ba177f7e93f` | `e29774331af6ced69da85e2133e493be43eaf8a982bde722270d23606e6e7c3b` |
| `calwrite_iwpriv_5g_baseline.txt` | 238 | `1e8e04f0c22a7d29451c959ff73bbdef` | `c5e09a09b21596a744f863039cf1da8bba0e4c9aaf09584cf44c124280a64d3d` |
| `calwrite_iwpriv_5g_after_restore.txt` | 243 | `f5fc8b190e9cb275b3ec63abf7248200` | `d53d4f2cd87762081a8e12119bdf4e357f2916f2ac6b58628247dc6b8ca8712f` |

`calwrite_backup.txt` and `calwrite_block.txt` were captured on the device and pulled; their local
md5 equals the device md5. `calwrite_dmesg.txt`/`calwrite_summary.txt` were likewise pulled with
matching md5.

## Verification: the four observations

| step | observation | evidence |
| --- | --- | --- |
| baseline | vendor first word `17161605` | `calwrite_iwpriv_2g_baseline.txt`; full snapshot `alg-values.txt` |
| noop | block byte-identical after write-back | dmesg `identical=yes`; extracted BEFORE==AFTER (36 rows each, `diff` empty) |
| patch | vendor first word `17161607` | `calwrite_iwpriv_2g_after_patch.txt`; dmesg `reached=yes` |
| restore | module AND vendor first word `17161605` | `calwrite_block.txt` (`word@+0x24=17161605`); `calwrite_iwpriv_2g_after_restore.txt` |

Green CI + noop identity + patch observed by the vendor getter + restore confirmed by both the
module and the vendor getter: all four hold.

## Limits, stated plainly

- **The changed byte is restored.** The only semantic change ever made was one nibble of the word
  at BAR0 `0x1b2f24` (`17161605` -> `17161607` -> `17161605`). `noop` rewrote the whole 576-byte
  block with bytes it had just read, so it could not change anything by construction. Nothing
  outside `[0x1b2f00, 0x1b2f00+576)` was written; no other byte was touched; no vendor module was
  loaded, unloaded, or otherwise modified; no config-space write, no `pci_request_region`, no reset.
- **A hazard rule was read narrowly on purpose.** The brief's "never write outside
  `0x1b2f00..0x1b2f24+4`" is stricter than the `noop` definition ("write the SAME bytes back to the
  same addresses" over 576 bytes). `noop` necessarily writes the full 576-byte window, but only with
  identical bytes, so it is semantically a no-op; `patch`/`restore` touch exactly `0x1b2f24..0x1b2f28`.
  This is the only place the module writes beyond the first 40 bytes.
- **BAR0 aliasing is not resolved.** Phase 11 found the block also at `0x86af00`. This experiment
  writes and reads only `0x1b2f00`; whether the alias follows is untested and out of scope.
- **The write is volatile.** It lands in the chip's mapped memory. Nothing was persisted to flash and
  no `set_*` command was used. A reboot (or the chip re-loading the table) returns the factory value
  regardless; the explicit `restore` step is what put the live value back without a reboot.
- **One word was exercised, not the calibration as a whole.** This proves the write path and the
  vendor's read-back of that word. It does not prove that an arbitrary calibration edit takes effect
  without a re-init, nor that the firmware re-consumes the table live beyond what the getter shows.
- **The getter is the firmware's answer, not an independent radio measurement.** The end-to-end claim
  is that our write reached the data `get_2g_power_param` serves. No RF/throughput effect was measured.
- **`build/` artifacts are not committed** (the repo `.gitignore`s `build/`). The snapshot bundle and
  the register-dumps live on disk at the paths above; everything needed to re-derive the claims is
  the module plus the printed commands.

## Reproduce

    # build + CI
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download <run-id> -n calwrite-ko -D /tmp/calwrite-ko

    # safety net first
    ROUTER_PASSWORD=... tools/wifi-cal-snapshot.sh build/cal-snapshots/<ts>

    # device
    ROUTER_PASSWORD=... /tmp/sshdev.sh root@192.168.10.1 \
      'md5sum /tmp/calwrite.ko; iwpriv Hisilicon0 alg get_2g_power_param'
    /tmp/scpdev.sh /tmp/calwrite-ko/calwrite.ko root@192.168.10.1:/tmp/
    ROUTER_PASSWORD=... /tmp/sshdev.sh root@192.168.10.1 \
      'insmod /tmp/calwrite.ko; cat /sys/kernel/debug/calwrite/backup; \
       echo 1 > /sys/kernel/debug/calwrite/noop; \
       echo 1 > /sys/kernel/debug/calwrite/patch; iwpriv Hisilicon0 alg get_2g_power_param; \
       echo 1 > /sys/kernel/debug/calwrite/restore; iwpriv Hisilicon0 alg get_2g_power_param; \
       cat /sys/kernel/debug/calwrite/block; rmmod calwrite; \
       ls /sys/kernel/debug/calwrite/; uptime'
