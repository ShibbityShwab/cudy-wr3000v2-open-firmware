# ERROR-PATHS - matrix, bench ladders and recovery policy

**Lane:** wifi-stability (task-136).  Definitions are machine-readable in
[ladders/error-paths.json](ladders/error-paths.json);
tools/error_paths.py audits every generated action against the project hard
rules and emits the attended script (--emit-script E2 > /tmp/e2.sh).

## 1. Matrix

| id | path | expected fail-closed behaviour | host coverage | bench ladder |
| --- | --- | --- | --- | --- |
| E1 | module rmmod/insmod cycling **without a release** | probe/remove lifecycle clean; no leak; chip still in ROM state (page+8 != 2); each load re-creates sysfs/wiphy; only legal before ANY release | stale-reload refusal + script audit; ladder script asserts no release write | E1: 3 x (insmod -> attrs/wiphy check -> rmmod), dmesg WARN grep, meminfo/slab delta, chip page read - STOP if page+8==2 or any release happened |
| E2 | AP up -> hostapd stop -> driver stop_ap -> hostapd restart (same boot) | default stop_ap_mode=0: stop returns 0 to cfg80211 with NO firmware write, restart re-posts AP config against the live VAP; mode 1: user_del (g0 id9) teardown then restart re-creates user_add/notify + VAP base; any message failure returns the error and keeps cfg80211 AP state (no fake success) | implementation + host checks | E2: golden readback -> AP-ENABLED -> kill hostapd -> readback/dmesg -> hostapd again -> expect AP-ENABLED + golden page; exception => stop + power-cycle |
| E3 | failed start -> retry | a CLEAN failure (builder/provider error before any AP frame) may be retried; a DIRTY failure (>=1 AP frame posted) latches ap_seq_dirty and refuses (-EIO) until power-cycle/VAP delete | policy gate host test (13 cases) | E3: hostapd with channel=14 (rejected before the driver, clean) -> fix to ch6 -> hostapd again -> AP-ENABLED |
| E4 | repeated C2 ladders across reboots | each boot identical: ctx_create type 1 + txq_sync72 consumed 0 polls, ok=2 timeout=0, mask/staging 0, same dmesg families | soak dry-run plan + analyzer fixtures | E4: N=3 cycles, each fresh boot -> C2 -> record h2d_status/rd_catalog/page/mask; PASS iff all cycles equal and clean |
| E5 | exception recovery (mask != 0) | detector refuses to continue: capture, power-cycle, no rmmod/retry; classify mask bits | analyzer FAIL-EXCEPTION verdict + recovery text | E5: read-only check script (--emit-script E5): prints page/mask/staging and the power-cycle card; exit 3 on exception; no writes at all |

Scan -> AP interaction (the 2026-11-27 A-D boots) is documented in
[ladders/scan-ap-diag.json](ladders/scan-ap-diag.json): A/D prove the stale
module-global base latch crashes the firmware after a scan + del/re-add; B/C
prove the restart -EALREADY blocker is scan-independent and exception-free.
Both are fixed by the hardening patch (see HARDENING.md).

## 2. Recovery policy (hard rules, unchanged by this lane)

| state | action |
| --- | --- |
| mask != 0 / staging != 0 / page lost | power-cycle (physical RESET + TFTP door); never rmmod; never post another frame in that boot; capture before the cycle |
| H2D announce timeout / lane closed | the core latches the lane closed for the load; no retry; capture, then power-cycle |
| hostapd AP stop / restart failure | mode 0 controlled stop is the supported restart; if the firmware page is not golden, treat as unknown state and power-cycle |
| module reload needed after a live boot | not supported: the stale-reload refusal fires (-EBUSY); power-cycle |
| any ladder step fails | stop at the first failure; one idea per boot; never retry the same write |

## 3. Operator sequence for a ladder

    cd opensource/docs/incoming/wifi-stability
    python3 tools/error_paths.py --dry-run --ladder E2
    python3 tools/error_paths.py --emit-script E2 > /tmp/e2.sh
    # attended: bench-link PASS, recovery armed, UART read-only, fresh golden boot
    tools/rsh.sh 'cat > /tmp/e2.sh' < /tmp/e2.sh
    tools/rsh.sh 'sh /tmp/e2.sh 2>&1 | tee /tmp/e2.log'

The emitted scripts refuse to run without the same golden-page gate and never
contain rmmod (except E1, which is explicitly pre-release and audited for it).
