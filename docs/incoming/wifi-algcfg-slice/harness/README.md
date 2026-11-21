# 17-twin oracle unit harness

Builds the same test source twice:

- **oracle_test** - links the LOCAL hi1105 reference wal_config_alg_cfg.c
  (the GPL-oracle; the reference tree has no license grant and is kept in
  /home only - oracle/ is gitignored). Setup:

      mkdir -p oracle/ref-shims
      cp <hi1105-wifi>/wal/wal_config_alg_cfg.c   oracle/wal_config_alg_cfg_ref.c
      cp <hi1105-wifi>/inc/hd_common/alg_cfg.h    oracle/ref-shims/
      cp <hi1105-wifi>/wal/wal_config_alg_cfg.h   oracle/ref-shims/
      # trim the reference copy after alg_cfg_args_analysis_hdtx_force_flag
      # (head -2204) - the tail needs the full hmac/mac headers.
      # ref-shims/ also carries the small header shims (see oracle build).

- **port_test** - links codec/wal_config_alg_cfg.c (the committed port).

Run:

    make            # builds both
    make check      # runs both and diffs the outputs

Result of this slice: diff is EMPTY (byte-identical), both exit 0,
"PASS: 0 failures" - 17/17 twins. Evidence logs: oracle-run.out / port-run.out
in the slice workdir (not committed; the command to reproduce is above).
