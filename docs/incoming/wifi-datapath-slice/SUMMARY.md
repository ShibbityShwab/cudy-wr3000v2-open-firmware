# Wi-Fi data-path slice (hdpp_/alg_) - SUMMARY

**Lane:** wifi-datapath-slice (host-only). **2026-11-22.** Full detail: REPORT.md,
code: scaffolding/, mapping tables: mapping/, regeneration: tools/gen-mapping.py.

## One-line verdict

The vendor hdpp_ (238 fns / 52,428 B) and alg_ (239 fns / 103,476 B) families are
now **mapped, annotated and scaffolded as original C**: a symbol-level inventory with
call-graph and arity evidence, a dpe_/alg_ twin mapping that quantifies the semantic
overlap honestly (4.2% exact-name coverage - the families, not the functions, are the
twins), behaviour annotations for the top symbols, and a compiling (-Werror-clean)
scaffold that re-exports **all 60 vendor ksymtab names (60/60)** so the port links.

## Key numbers

- hdpp_ -> dpe_: **3 of 238 exact-name twins (1.3%, 532 B)**; the engines are the same
  role with fully diverged naming (hdpp band/device/vap/user object tree + stat dfx vs
  dpe config/sync/thread architecture).
- alg_ -> alg_: **17 of 239 exact-name twins (7.1%, 5,776 B)** in the generic
  cfg-command core (wal_config_alg_cfg.c family); the other 222 are hi5622-specific
  parsers/formatters (ersru/trigger/spectral/smartant/ulsch/ofdma/atf/...).
- Combined: **20 of 477 exact twins (4.2%) = 6,308 of 155,904 B (4.0%)**; 174 of 477
  symbols have measured call-site arity evidence.
- Export surface: 46 hdpp_stat_* dfx + 6 alg_* + 8 g_alg_* names; wifi_debug.ko
  (which imports 62 hdpp/alg names) is the consumer that pins this ABI.
- Scaffold: hi5622_hdpp_alg_skel.ko **73,404 B**, vermagic 6.18.60, clean build,
  143 hdpp/alg text symbols, all 60 vendor exports covered (+2 intentional).

## What exists now

- mapping/hdpp-to-dpe.csv + alg-to-twin.csv - per-symbol twin, score, shared
  callers/callees, reference shape metadata.
- mapping/hdpp-inventory.csv + alg-inventory.csv - every symbol: size, exported
  flag, measured arity/retuse, top callers.
- mapping/coverage.csv - the coverage summary above.
- scaffolding/hi5622_hdpp.h + hdpp_skel.c, hi5622_alg.h + alg_skel.c, Kbuild,
  BUILD-NOTES.md - documented TODO stubs, provisional signatures where the bench
  must decide.
- REPORT.md section 6 - the 12-point bench-probe list (init dispatch, sync-write
  D2H acks, counter layouts, D2H TWT/ERSRU payloads, alg table contract, ...).

## Next slice

alg_cfg + wal config-command port first (self-contained string codec, host-only
testable, 17 twins as oracle), then hdpp object model + stat dfx (needs transport
bench stage B2).  Details in REPORT.md section 8.

## License

All text here is original writing by this lane (scaffolding GPL-2.0, matching the
kernel it links against).  The hongmengkernel hi1105 tree was used as a
name/structure reference only (no license grant; no text vendored); no vendor
binaries or blobs are committed.  Commit: see git log (submodule opensource).
