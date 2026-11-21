# Wi-Fi alg_cfg / wal config-command codec port - SUMMARY

**Lane:** wifi-algcfg-slice (host-only). **Commit:** opensource submodule 593e8d1.

## Result

| gate | result |
| --- | --- |
| hi3881 GPL-2.0 tree fetched into /home | [V] (depth-1 clone, not committed) |
| wal/wal_config_alg_cfg.c located | [V] - **hi1105 tree only, no license grant**; used as local execution oracle; NOT vendored |
| GPL-2.0 vendored text committed | [V] vendor/hi3881-wal/ (wal_hipriv/ioctl/customize/maintenance, headers kept) |
| 17-twin oracle harness (host) | [V] **PASS 17/17 - port output byte-identical to oracle, 0 failures** |
| 6.18.60 module build | [V] hi5622_alg_cfg_codec.ko, 97,108 B, GPL, vermagic 6.18.60 SMP mod_unload ARMv7 |
| divergence annotation | [V] 414-entry process table dump + 40-entry param-size dump + 176-B LUT shape + per-twin notes (mapping/) |
| hi5622-specific census | [V] mapping/hi5622-specific.csv (120 annotated: spectral 30, smartant 6, ulsch 5, trigger 15, ersru 3, sounding 2, 60 formatters, ...) |

## Key findings

- The binary's alg codec is the same table-driven text-command design as the
  reference but with a **414-entry process table** (ids 201..4401), a
  **176-B-per-command LUT dispatch** (416 entries, key (type<<16)|id) instead
  of the 8-B parser table, and **runtime-patched command names** (a 4608-pair
  custom fixup list in .data; standard relocations are absent for the table
  slots - name mapping is a bench probe).
- All 17 twins MATCH the reference semantics; binary error codes pinned
  (100/101/1009/1014/1017/35629); the reference's int8 wrap quirk in
  waterfilling_limit is reproduced faithfully (and oracle-verified).
- The task premise ("wal_config_alg_cfg.c is in the GPL-2.0 hi3881 tree") is
  wrong; the codec core is committed as original GPL-2.0 text pinned to the
  binary oracle, per the clean-room rules of this notebook.

## Next slice

wal config-command glue + hmac cfg dispatch (wire the codec into
wal_cfg80211/hipriv on 6.18; LUT completion probe; hi5622 parser families).
