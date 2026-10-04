# SCRATCH BOOT: enable(0x4c) EXECUTED in the takeover boot - the enable half is settled (phase 49, 2026-10-04)

Task 11 of the wifi-forward plan: the decisive L3 measurement. Two scratch cells patched into a firmware
copy record, in live memory, whether the enable thunk and the distributor bring-up path ran. Evidence
`build/register-dumps/exp/20261004-165139/` (diag polls in `build/register-dumps/exp/20261004-165044-diag/`).

## The measurement

`capture-cmd.txt` reads the eight cells at BOTH BAR0 aliases (BAR0 and BAR0+`0x6b8000`) after the port's
done marker, plus the two blob md5s:

| cell | BAR0 addr | BAR0 value | alias addr | alias value | task-9 baseline | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| S1 | `0x401035A0` | `0x00001000` | `0x407BB5A0` | `0x00001000` | `0x00000000` | WROTE (bitmap word) |
| S1+4 | `0x401035A4` | `0x40161108` | `0x407BB5A4` | `0x40161108` | `0x00000000` | WROTE (dest 0x40161108) |
| S2 | `0x40103EB4` | `0x00000001` | `0x407BBEB4` | `0x00000001` | `0x00000000` | WROTE (bring-up state) |
| S2+4 | `0x40103EB8` | `0x50AA7E49` | `0x407BBEB8` | `0x50AA7E49` | `0x00104427` | WROTE (marker by equality) |

`build/register-dumps/exp/20261004-165139/capture-cmd.txt` and `interp.txt`.

- **S1 = `0x00001000`** is the computed bitmap word (bit 12 -> source id `0x4c`), and **S1+4 =
  `0x40161108`** is the distributor enable destination - exactly the pair the scratch(i) enable-thunk
  trampoline stores when it passes the `cmp r4,#0x1000` gate inside the enable thunk (`interp.txt`). The
  enable half executed.
- **S2 = `0x00000001`** and **S2+4 = `0x50AA7E49`** (the scratch(ii) bring-up marker) show the distributor
  bring-up store ALSO fired - strictly more than the table's row-1 minimum, which excludes the
  partial-init alternative (that row needs S1 absent).
- **Aliases agree** for all four cells: the two decode paths return identical values, so neither read is a
  window artifact. No cell reads `0xffffffff`, so both windows decoded (`0xffffffff` would have been a
  harness defect, not a result).
- Staged blob md5 at capture `b08699bd902d35e697e24b94edb6fa2c` (== `build/tmp/fw-patched/scratch.bin`);
  stock md5 `0e530b976d5a20e87358671f1a577695` (untouched). `[sig] 9/9` present.

## The equality rule (why "non-zero" would have proven nothing)

Task 9 measured S2+4 = `0x00104427` in the takeover image and, with the seven-offset barmap
corroboration, fixed its provenance as STATIC frozen-image content, not a runtime write
(`build/register-dumps/exp/20261004-163526/scratch-baselines.txt`, carrying the task-9 verifier's
post-verification correction). So the run asserts the marker BY EQUALITY: `S2+4 == 0x50aa7e49`. A "!= 0"
test would be permanently true and prove nothing (`interp.txt`).

## The verbatim plan table and the matched row

`interp.txt` quotes task 11's four-row table verbatim and names exactly one match: **BRANCH-1 - "S1/S1+4
present (bitmap word + dest 0x40161108) -> enable(0x4c) executed: the enable half is settled and the
remaining failure is the delivery hop."** The other three rows are each excluded: partial-init needs S1
absent (S1 is present); init-never-ran needs both cells absent (both are written); enable-present/
bring-up-absent needs S2+4 != marker (S2+4 == `0x50aa7e49` by equality, so the bring-up path ran).

## Live corroboration (not a capture-time artifact)

The round-2 poller read the same cells mid-init, read-only over ssh
(`build/register-dumps/exp/20261004-165044-diag/`). `poll-05.txt` at uptime 44.35 s: symlink consumed,
watchdog armed, `insmod ... fwpath=...omo-pat` running, the 26-write binding done, and the eight cells
ALREADY WRITTEN; `poll-06.txt` at 57.02 s identical. `poll-01.txt` (normal vendor boot, pre-reboot) shows
the task-9 baseline; `poll-04.txt` (takeover, uptime 31.8 s) shows all eight `0xffffffff` before the port
programs the windows. The sequence baseline -> `0xffffffff` (pre-programming) -> patch values -> baseline
on the recovered boot is pinned live, so the capture is not an artifact.

## The round-1 stall (recorded, not a branch)

Round 1 (`2026-10-04T16:45:16Z`, no evidence dir, `EXP RESULT: FAIL "run: device did not return fresh or
experiment never finished"`): the takeover boot never loaded the staged module - no `omo-drv1` dmesg line,
no lsmod entry, no new pstore record - so no scratch cell was written and the eight cells still read the
task-9 baselines. It is a harness/loader stall (`diag-report.txt`), not a task-11 branch. Round 2's live
polls pinned the full working sequence. The reuse of the harness across a stale boot is the
reboot-freshness-gate artifact the round-2 diagnostic names; a later fix hardened the gate.

## What this settles

The enable half of the last link is measured: `enable(0x4c)` executed in the takeover boot. What remains is
the device-internal delivery hop from the ctrl-rb's masked status to the GIC input - the same open hop
phase 47 named. GATE G4's preconditions hold (BRANCH-1 + `9/9` + a healthy recovery path).

## Health

`health.txt`: `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`;
`acceptance.txt` ends `ALL_OK`.
