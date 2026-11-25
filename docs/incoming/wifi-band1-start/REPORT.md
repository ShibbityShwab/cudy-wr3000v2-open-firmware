# REPORT - the band-1 start arming: how the stock 5 GHz MAC starts without G+0x370

**Lane:** wifi-band1-start (shared task **task-197**). **HOST-ONLY**: static
decode of the pinned local artifacts; no device contact, no write, no fire.
**The Lead fires.** Companions: [README.md](README.md), [TABLES.md](TABLES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/fw_band1_start_re.py](tools/fw_band1_start_re.py),
[evidence/](evidence/).

Grades: **[V]** re-derived by the committed tool from the pinned artifacts;
**[V\*]** bench-measured by a committed lane; **[I]** inference; **[U]** needs
a device read or a stock H2D trace.

---

## 0. Answer in one paragraph

The stock does **not** arm the 5 GHz (band-1) MAC start through the VAP-start
hook G+0x370. That slot has no writer anywhere in the image and a stage outside
the image cannot know this image's callback CAs, so the stock's slot is also
NULL [I]. The band-1 start is reached by **direct calls to CA 0x4AF0C**, and the
two call sites that are inside H2D config-event workers are the host-reachable
ones: **key 0x0242** (WAL `set_green_ap`, worker CA 0x4C524, subcommand in
`body[0]`: 1 = sync start, 2 = forced start, 3 = stop) and **key 0x0243**
(worker CA 0x8419C -> helper 0x4BDCE -> 0x4BEB0; arms only when the band state
marker is 3). Our F-ladder never posts either family, which is exactly why
band-1 never left the state R3 measured. R3b should create a fresh
band-1-bound module context (key 0x0064, `data[4]` = N, `data[0x12]` = 1)
and then post key 0x0242 with field B = N and `body = {01,00,01,00,00}`,
checking 0x4010C220 for 0x1029 (the probe pack and rungs are in
[FIRE-RECIPE.md](FIRE-RECIPE.md)).

## 1. Method and inputs

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b` |
| CPU convention | CA = image file offset + 0x40000 (CPU 0..0x40000 is not in the image) |
| CPU RAM phys | 0x40000000 + CA + 0x6B8000 |
| device IO phys | CA + 0x3B8000 |
| vendor host module | `hi5622v100_wifi.ko` (3,581,748 B, keys/symbol table, read in place) |
| vendor boot modules | `hi5622v100_plat.ko` + the `hi_*.ko` set (read in place) |
| dispatch model | g0-id1 handler CA 0x43D58; key = u16 hdr+0x14, worker from table A CA 0x10C23C (217 rows) or table B CA 0x103BF8 (88 rows); worker(ctx, len, data), ctx = 0xFE578(0, hdr[3]) |

The tool re-derives every [V] claim from the local image: `--start` (the
0x4AF0C decode), `--xrefs` (the five call sites and their keyed triggers),
`--hooks` (the G+0x370 no-writer recheck), `--keys` (key 0x0242/0x0243/0x0073
worker decode), `--probe` / `--recipe` (the fire pack),
`--modules` (host-module constant scan). `--selftest` = 47 assertions.

## 2. The start function CA 0x4AF0C [V]

CA 0x4AF0C is the per-band MAC start that produces the R3 differential. It:

1. reads `r1` = the per-band run-state (band object + 0x14D0); returns early
   when `[state+1] == 2` ("already started", CA 0x4AF2C) or when
   `[state+4] == 0xFF` and when `0x961AE([state+4])` (the bound VAP index)
   does not resolve;
2. takes the band index from `u8(*(bandobj+0x1A00) + 0x11D)` (CA 0x4AF5C);
3. **band 1**: resets the band-1 MAC block at 0x40114500 (0x55/0xFF), checks
   0x4011451C, then **ORs 1, 8, 0x20, 0x1000 (= 0x1029) into 0x4010C220**
   (0x4AFB2..0x4AFD4), ORs 1 into 0x4011080C or 0x4011480C (gate), ORs the
   band-1 bit into 0x40101220, and calls 0xF4504 / 0xF5C54 / 0x96DBC;
4. **band 0**: the same shape with **0x40108214 |= 0xF** and
   0x40110500 reset; note 0xF is also the final value of the band-0 probe
   helper (section 6), so the band-0 read alone does not prove which path ran;
5. marks `[state+1] = 2` and increments the start counter `[state+0x64]`.

The stop counterpart CA 0xBE294 clears the same bits; it is called from
0x4C27A and 0x4C6A0 only. [V]

## 3. The five direct call sites - and the keyed paths behind them [V]

`0x4AF0C` has exactly five direct `BL` sites and no literal pointer:

| site | enclosing | trigger |
| ---: | --- | --- |
| 0x4BEB0 | 0x4BDCE | helper called by keys **0x0243**, **0x01FE**, VAP-down 0x0067, and internal paths 0x644A4 / 0xB8528 / 0xB9780 |
| 0x4C34E | 0x4C05C | internal state machine 0x4C05C <- 0x5A3BC <- 0x5A588 [U] |
| 0x4C3EC | 0x4C3AC | 0x4C3FC/0x4C626; 0x4C3FC is called by key 0x0067 (0x46098), key 0x0065 (0x4674A) and key 0x0242 (0x4C5F4) |
| 0x4C612 | **0x4C524 key 0x0242** | `body[0]=1` sync/conditional start |
| 0x4C652 | **0x4C524 key 0x0242** | `body[0]=2` forced start |

Key 0x0242 is the WAL `set_green_ap` command: host emitter
`wal_config_set_green_ap` (wifi.ko .text+0x12AE20) calls
`hmac_config_send_event(ctx, 0x0242, len, data)` [V]. Key 0x0243's emitter is
`wal_config_recently_cfg_event`. Neither appears in our clean host ladder; the
0x0065/0x0073 path only reaches CA 0x971D8 and therefore the NULL G+0x370
hook. That is the concrete gap R3 measured.

## 4. The G+0x370 question: boot prelude and host modules [V/I]

* **No writer in the image.** Re-verified in this lane: no literal 0x174B34 or
  0x174B38 anywhere; the 19 image stores with disp 0x370 all have non-G bases
  and none is inside the installer block; the installer CA 0x5B424 (called by
  the pre-HCC boot init at 0x48442) writes the five other slots and never
  0x370. The only readers are the three calls at 0x9735C..0x97372.
* **No boot-prelude write is possible.** The image is linked at CA 0x40000, so
  the CPU's 0..0x40000 window is not in FIRMWARE.bin at all (SoC ROM / ITCM
  region). More importantly, a pre-image stage has no way to know this
  firmware's callback CAs (0x565C5, 0x57639, ... are image code addresses), and
  the installer already proves the hook block is image-owned.
* **No host module installs it.** plat.ko parses and downloads FIRMWARE.bin
  as-is over PCIe (`firmware_download` -> `firmware_file_send` ->
  `bal_write`); it carries no firmware G/callback constant. The scan of every
  vendor .ko finds no LE firmware CA (the one big-endian byte pattern in
  wifi.ko is unrelated data). The WAL/driver side only carries config-event
  keys.
* **Conclusion [I]:** the stock slot is NULL too; G+0x370 is not "missing on
  our boot" but "unused by design" - the task-186 verdict stands, and the
  earlier sentence "C5a should be retired" is correct only in that sense: the
  missing arming is real, but it is not a boot/host install step.

## 5. Fresh band-1-bound module context (key 0x0064) [V]

Worker CA 0x449E4 decodes the bind: `body[0]` = create type (2 = AP),
`body[1]` = parent module index (0x12), `body[4]` = module/VAP index N
resolved through 0x961AE, `body[0x12]` = band selector through 0xA4FC8 to
the band object (band 1 = 0x15E570). When `*(bandobj+0x1A00)` (published
per-link) is nonzero, it binds **ctx(N)+0x4B0 = perlink, +0x4B8 = band object,
+0x4B4 = VAP** (CA 0x44BBC/0x44BC2/0x44BC6). It does **not** call 0x4AF0C.

Two consequences for R3b:
1. a fresh context only helps if the following frame actually starts the band -
   hence keys 0x0242/0x0243 (section 3); the fixed 0x0064..0x0065 ladder alone
   cannot arm 0x4010C220;
2. the run-state struct is at `bandobj+0x14D0`, i.e. **per band, shared by
   all module contexts**. A fresh context does not reset the "already started"
   marker [state+1] or the "no bound VAP" marker [state+4]; probe both before
   and after the create.

## 6. The enable-TRX / disable-TRX processor and the band probes [V/U]

CA 0xE8520 is a message processor (the ISR-name blob lists `enable_trx` /
`disable_trx`); selectors are `r6 = u8([r1+0xA])` and `r5 = u8([r1+0xE])`:

* `r5==0, r6==1` calls 0xD97BC (band-0 MAC reset at 0x40110500) and 0xD973C
  (band-0 probe: writes 4,5,7,**0xF** to 0x40108214 and checks the 0x40108228/
  0x4010822C readback) - this is a second explanation for the live band-0 0xF;
* `r5!=0, r6==1` calls 0xD968C (band-1 reset at 0x40114500) and 0xD9604
  (band-1 probe: writes 0x10,0x18,0x38,0x1038,**0x1039** to 0x4010C220 and
  checks 0x4010C238/0x4010C23C) - note 0x1039, not the R3 arm 0x1029;
* only in the `r6==1, r5==1` branch does it write the band-1 TRX triple
  0x4010E150 = 0x33, 0x4010E154 = 0xFF, 0x4010E158 = 0xFF (the only writer of
  that triple in the image).

Open point [U]: R3 sees the triple set (so some branch ran for band 1) while
0x4010C220 reads 0 (so the band-1 probe/reset either did not run in that
invocation or was cleared later by a band-1 MAC reset). Which selector pair
stock uses, and which host message carries it, is not yet decoded - no g0-id1
table A/B row reaches 0xE8520 and the host-module scan found no dedicated
emitter. Do not poke these registers blind; the P1 pack records them.

## 7. Limits / what is still [U]

* which of the five paths the stock actually uses at 5 GHz boot (the keyed
  ones are the candidates; a stock H2D trace would settle it);
* the exact green-AP body the stock userspace sends (subcommand + timing
  bytes); the rung uses the minimal `{01,00,01,00,00}` shape derived from the
  worker's own reads;
* the selector semantics of 0xE8520 and whether the TRX triple path can be
  driven from the host;
* whether the band-1 MAC block reset during a start clears an earlier probe
  value (explains the R3 0x4010C220 = 0 vs triple set discrepancy).

None of these blocks the R3b decision: the direct start path is decoded, the
frame format is known from ../wifi-vap-start, and the probe pack discriminates.

## 8. Reproduction

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
$PY tools/fw_band1_start_re.py --selftest    # 47 PASS, 0 FAIL
$PY tools/fw_band1_start_re.py --start       # = evidence/start.txt
$PY tools/fw_band1_start_re.py --xrefs       # = evidence/xrefs.txt
$PY tools/fw_band1_start_re.py --hooks       # = evidence/hooks.txt
$PY tools/fw_band1_start_re.py --keys        # = evidence/keys.txt
$PY tools/fw_band1_start_re.py --probe       # = evidence/probe.txt
$PY tools/fw_band1_start_re.py --recipe      # = evidence/recipe.txt
$PY tools/fw_band1_start_re.py --modules     # = evidence/modules.txt
```

No vendor binary, firmware byte or per-unit value is committed; the tool reads
the pinned local artifacts at run time and prints only derived addresses,
mnemonics and table words.
