# wifi-wal-glue - SUMMARY

Host-side slice: wires the alg_cfg command codec (wifi-algcfg-slice,
17/17 oracle PASS) into a 6.18 wal config-command front end.

**Build:** hi5622_wal_cfg.ko - 222,732 B, GPL, vermagic 6.18.60 SMP
mod_unload ARMv7 p2v8, depends=cfg80211, 0 errors / 0 warnings against the
luofu 6.18.60 tree (codec linked into the module).

**Deliverables:**
- glue/ - wal_cfg_glue.h + 4 C files + Kbuild + cfg80211-provider.symvers
  + README (front end = wal_wlan_cfg_module_process_entry{,_host,_device}
  dispatch, hipriv-style wal_hipriv_alg_cfg, sysfs /sys/kernel/wal_cfg/,
  cfg80211 vendor-cmd registration helper, hmac cfg dispatch stub surface
  with 22 vendor names, 416x176-B LUT + name-fixup loader, vendor-exact
  14-name alg export surface + glue extras).
- probes/bench-probes.md - P0..P6 bench probe list extending the algcfg
  probe list (name harvest, LUT completion, param-size names, formatter
  arities, parser oracles).
- mapping/dispatch-surface.csv - 44-row vendor-name -> glue-symbol map.
- REPORT.md - full write-up + the exact next bench-gated step.

**Remaining for the 120 hi5622 parser families:** 60 formatters (P2+P4),
30 spectral + 6 smartant + 5 ulsch + 5 trigger + 3 ersru + 3 ns_aggr +
3 mumimo + 2 sounding + 3 misc parsers (P5), plus the cali entry and the 4
vendor-exported stub functions - all bench-gated on probes P1 (names) and
P2 (LUT field map).

**Commit:** submodule opensource (see git log); host-only, no device writes,
text only, no hi1105 text, no blobs.
