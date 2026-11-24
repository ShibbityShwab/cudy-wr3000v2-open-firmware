# wifi-sta-ops (task-149) - AP client station lifecycle + EAPOL/data RX

Host-only lane of the Cudy WR3000 v2.0 Hi5622 Wi-Fi reversal.  This lane:

1. static-RE's the **vendor station lifecycle** - the cfg80211 station ops
   (stubs / del kick), the hmac AP-SME client-add path, the **per-STA
   firmware context** (g0 id7 user_add + g0 id1 cmd 0x07d2 association
   state, with the PD4 firmware table-A row and worker semantics decoded),
   and the **EAPOL/data RX** conversion+detection rules;
2. implements it on the **FINAL ALL-PATCH module** as
   patches/0001-sta-ops.patch: .add_station/.change_station/.del_station
   + real .get/.dump_station, the firmware per-STA context builder, and an
   EAPOL/data RX delivery hook in the rx-static drain - all fail-closed at
   sta_ops=0 / eapol_rx=0;
3. ships host tests (112 literal wire/parse checks), a reproducibility tool
   for the vendor/firmware facts, a cross-compiled build gate, and an
   attended ladder.

**Status:** host-complete, cross-compiled (50 params, 0 warnings, strip
.text identical), **NOT fired**.  The full 4-way handshake additionally
needs host -> AP data TX (hostapd EAPOL msg1/msg3); this lane accounts that
drop and hands the TX gap to the next lane (REPORT section 7, ladder L4).

## Files

| path | what |
| --- | --- |
| REPORT.md | the RE story, implementation, integration state, limits |
| TABLES.md | all decoded wire/address facts with evidence markers |
| LADDER.md / ladders/sta-assoc-eapol.json | attended ladder (design only) |
| patches/0001-sta-ops.patch | the module patch (9 files, original C) |
| src/ | the lane's module sources (host-testable logic + kernel glue) |
| tools/sta_ops_selftest.c | portable builder/parser host selftest |
| tools/sta_ops_re.py | re-derive the vendor/firmware facts locally |
| tools/build-sta-ops-module.sh | apply+build identity gate (host-only) |
| tools/run-host-check.sh | one-command host gate |
| evidence/ | recorded host-check, build and RE evidence |
| refs/artifact-identity.txt | base/lane artifact identity |

## Quick verification (host only; no device, no vendor artifacts needed)

    ./tools/run-host-check.sh
    ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-sta-ops-module.sh
    STA_OPS_KO=<local hi5622v100_wifi.ko> STA_OPS_PD4=<local PD4> \
        .provision-venv/bin/python tools/sta_ops_re.py "$STA_OPS_KO" "$STA_OPS_PD4"

The vendor module, the PD4 firmware and the built .ko are local/gitignored
evidence - they are never committed, and the lane contains no vendor bytes,
no calibration data and no secrets.  Do not fire anything without an
attended session, the recovery door READY, and the Lead's bench procedure.
