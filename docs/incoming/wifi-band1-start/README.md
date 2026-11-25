# wifi-band1-start - how the stock arms the 5 GHz (band-1) MAC start

**Lane:** shared task **task-197**. **HOST-ONLY, read-only**: this lane read the
pinned local vendor firmware image and vendor modules in place; it never
contacted the router, never flashed and never fired. **The Lead fires.**

Companions: [REPORT.md](REPORT.md), [TABLES.md](TABLES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/fw_band1_start_re.py](tools/fw_band1_start_re.py),
[evidence/](evidence/). Predecessors: ../wifi-fw-tbtt-arm (R3 differential +
the 0x1029 / 0xF / TRX-triple registers), ../wifi-fw-callback-install (the
G+0x370 no-writer proof), ../wifi-fw-beacon-tx (VAP start -> 0x971D8),
../wifi-vap-start (key 0x0064/0x0065 frame formats).

## Verdict

1. **The 5 GHz start does not need G+0x370.** The hook slot has no writer in
   the whole image (re-verified), and a stage outside the image (SoC ROM /
   boot prelude / u-boot / host modules) cannot know this image's callback CAs
   - so the stock's G+0x370 is **also NULL** [I]. The band-1 MAC start is
   reached by **direct calls to CA 0x4AF0C**, not through the VAP-start hook.
2. **The host-reachable arming paths are H2D config events.** The start
   function has exactly five direct call sites
   (0x4BEB0 / 0x4C34E / 0x4C3EC / 0x4C612 / 0x4C652). Two of them are inside
   table-A workers:
   * **key 0x0242** ("WAL set_green_ap", worker CA 0x4C524) - subcommand in
     `body[0]`: **1 = sync start** (0x4C612), **2 = forced start** (0x4C652),
     **3 = stop** (0xBE294). This is the cheapest new rung for R3b.
   * **key 0x0243** (worker CA 0x8419C -> helper 0x4BDCE -> 0x4BEB0); the same
     helper is also called from the key 0x01FE worker and the key 0x0067
     VAP-down path. It arms only when the band state marker [state+1] == 3.
3. **Our ladder never posted 0x0242/0x0243** (the F-ladder is
   0x0064/0x0065/0x0124/0x0127/0x01F7/0x00F7), so band-1 was never started -
   this is consistent with R3 and is the open fix. R3b should create a fresh
   band-1-bound module context (key 0x0064: `data[4]` = free module index N,
   `data[0x12]` = 1) and then post key 0x0242 with field B = N and
   `body = {01,00,01,00,00}`.
4. **The enable-TRX / disable-TRX message processor (CA 0xE8520) is the only
   writer of the band-1 TRX triple** 0x4010E150 = 0x33 / 0x4010E154 = 0xFF /
   0x4010E158 = 0xFF; it also calls the band probe helpers. It does **not**
   write the R3 arm value 0x1029, and its sub-selector semantics are still
   [U]; no g0-id1 key reaching it is known. Do not invent one.
5. **No vendor boot module installs a firmware callback**: plat.ko downloads
   FIRMWARE.bin as-is (`bal_write`), and a scan of every vendor .ko finds no
   firmware G/callback constant. The stock trigger for the direct start is a
   host config event (WAL/driver), not a boot-prelude write.

## Files

| file | content |
| --- | --- |
| `REPORT.md` | the decode: start function, five call sites, keyed paths, G+0x370 boot analysis, fresh-context analysis, limits |
| `TABLES.md` | instruction/register/key tables (derived addresses only) |
| `FIRE-RECIPE.md` | read-only probe pack + the R3b-extension fire rungs + abort rules |
| `tools/fw_band1_start_re.py` | reproducible tool (selftest 47/47): start decode, xrefs, hook-block recheck, key table, TRX worker, module scan, probe pack, recipe |
| `evidence/` | tool outputs (derived values only; no vendor bytes) |

## Reproduce

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
$PY tools/fw_band1_start_re.py --selftest    # 47 PASS, 0 FAIL (capstone 5.0.7)
$PY tools/fw_band1_start_re.py --start       # and --xrefs --hooks --keys
$PY tools/fw_band1_start_re.py --probe       # and --recipe --modules
```

Artifact (local, uncommitted): `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`,
928,920 B, sha256 `7fc87e20...311b`; CPU = file offset + 0x40000;
firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO phys = CA + 0x3B8000.

## Boundary

No vendor binary, firmware byte, extracted table, credential or per-unit value
is committed; the tool reads the pinned local artifacts at run time. No device,
bench link, UART, network access or fire was used.
