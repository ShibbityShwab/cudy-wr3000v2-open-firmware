# wifi-rom-dump - the chip mask boot ROM reversed: S0/S1, the +8=2 writer, the 0xCEED gate and the host action

Host-only lane (`task-86`, 2026-11-23). Read `REPORT.md` for the full reconstruction; this
file is the short index.

## What is here

| file | purpose |
| --- | --- |
| `REPORT.md` | the reconstruction: ROM map/classification, S0 reset path, CPU0 primary path, CPU1 S1 path, the exact +8=2 writer, the ```0x4000010C``` dual-use gate, image FIQ/IRQ path, corrections to prior lanes, host-action answer, bench plan, evidence table |
| `tools/romdis.py` | self-checking ROM + image analyzer: `selftest` (86 checks), `info`, `map`, `flow`, `stores`, `consts`, `tables`, `search`, `gate`, `image `{`header|handlers|dispatchers|census`} |

## Headline results

* **The readable 16 KiB window is the whole relevant mask ROM**: 2,073 bytes of content
  (`0x000-0x81B`) then zero/RAZ to `0x3FFF`. Vectors: reset `0x8C`; **IRQ -> image
  trampoline `0x40064`**; **FIQ -> ROM stub `0x7C` -> image header +0x50 = `0x103140`**.
* **S0**: caches/protector off, per-mode stacks (`0x1BF110/0x1BEF00`-relative), CP15
  c6 MPU-style region config (4 GB / 4 MB IO / 2 GB), protection on, `MPIDR&3` split into a
  primary (CPU0) and a secondary (CPU1) path.
* **CPU0 primary path** (ROM `0x50C/0x54C`): clear the stale image header entry-A pointer,
  run the cold fallback (write `0xBAAD` to `0x4000010C`, analog/PHY init, poll
  `[0x40000108]==0x5A5A` = host release), write `0xCEED`, then **re-read `[0x40000]` and
  `blx` image entry A (`0x46970`)**. The host must upload before the release write.
* **CPU1 secondary path = S1** (ROM `0x564`): clear its words, record the `0xCEED` check,
  clear the stale entry-B pointer, arm, then WFI until **`[0x1BF00C]==0xABCD` and
  `[0x4000010C]==0xCEED`**; on success it writes **`[0x1BF008] = 1<<core = 2`** at ROM
  `0x5FE` and **`blx`-es image entry B (`0xC742C`, from CPU `0x40004`)**. The image
  never writes 2 - it consumes the ACK.
* **The gate is a single 16-bit register used twice**: ROM boot stage wants `0xCEED` (written
  by the CPU0 fallback after the host release); the image service/exception path wants
  `0xCECE` (image `0xC20F8` / `0xC6E14` spins). **Our transport writes `0xCECE` there
  during boot, overwriting `0xCEED`**, so CPU1's loop can never pass. The vendor host never
  writes `0xCECE` and its boot has mask 0, so `0xCEED` survives.
* **Host action (definitive): release only.** Write `0x5A5A` to CA `0x40000108` after the
  usual pre-release seeding and **do not write `0xCECE` to CA `0x4000010C` during boot**;
  write it only after S1 is confirmed (`page +8 == 2` / bit14 set). Optional repair: if
  `+8` stays 0, read `0x4000010C` and write `0x0000CEED` back (CPU1-only consumer).
  Expected success reads: page `0x86807FEF / 0x7 / 0x2 / 0xABCD`, state `0x118B60 = 1`,
  mask `0x40008 = 0`.
* **Bench cross-validation**: the previously unexplained pre-upload page `{+0=7,+4=3,+8=random,
  +C=0}` is exactly the ROM state (CPU0 parked in the release poll, CPU1 parked in the WFI
  loop). The golden page `{0x86807FEF,7,2,0xABCD}` is the same path completed.
* **Corrections to prior lanes** (all in `REPORT.md` section 9): `0x7386F44F` is code not a
  magic; `0x1F4/0x1F8` are stack tops not handler slots; header order is
  {FIQ, data-abort, prefetch-abort, SVC, undefined}; r1 ids are undef=1, svc=2, prefetch=3,
  abort=4 (so bench mask `0x0A` = undefined+prefetch, not svc+data-abort); entry B is
  dispatched by CPU1 after S1; the IRQ vector does point into the image trampoline; and S1 is
  host-unblockable, correcting the "host cannot complete S1" conclusion.

## Reproduce

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-rom-dump/tools/romdis.py

$PY $T info          # identity / sha256 / region classification
$PY $T map           # vectors, stubs, slot table, code/data map
$PY $T flow          # S0 / primary / secondary S1 reconstruction
$PY $T stores        # absolute-address store census
$PY $T consts        # materialised-constant census
$PY $T gate          # S1 gating analysis + host-action checklist
$PY $T selftest      # 86/86 PASS
```

Env overrides: `ROM_DUMP` (default `/home/shibbityshwab/staging-fi/rom16k.bin`) and
`HI5622_FW` (default the local vendor `FIRMWARE.bin`). The tool prints only derived
addresses/constants/disassembly; neither the ROM nor the firmware is copied into the repo.

## Local evidence only

The ROM dump is a DEVICE ROM (16,384 B, sha256 `fcfd464f...98faa4`) and is NOT committed; the
vendor firmware is NOT committed. Nothing in this directory contains ROM or firmware bytes.
