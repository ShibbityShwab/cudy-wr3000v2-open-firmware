# TEST-LADDER - cfg80211 scan / AP / VAP userspace ladder (wifi-scan-ap lane)

**Lane:** wifi-scan-ap (shared task **task-113**), host-only delivery + attended
bench procedure. **Date:** 2026-11-24.
**Status:** host-only. The M0 wiphy / M1 netdev lifecycle it builds on is
hardware-validated (STATUS.md sec 3k, 2026-11-25); the scan/AP wiring delivered
here is **compile-verified and host-selftested only, never bench-fired**. Every
step below is written so the expected *failure* is as informative as the
expected success: the driver must never fake a scan or an AP start.

## 0. Why most of this ladder is a NEGATIVE test today

| dependency | state | consequence |
| --- | --- | --- |
| wiphy registration after the HCC ready handshake | **hardware-validated** 2026-11-25 | `iw phy`, `iw dev`, VAP add/del work |
| frame provider (`hi5622_frame_ops_register`) | **absent** in the committed module (registry empty) | `.scan` / `.start_ap` / `.stop_ap` / VAP firmware messages return `-EOPNOTSUPP` |
| scan request g0 id0xC + D2H g1 15/16/17 / g2 2 payloads | **[U]** - wifi-vap-scan lane (task-110) | even with a provider: builders return `-EOPNOTSUPP` until decoded |
| WAL group-2 AP-start payload (gap G1) | **[U]** | `start_ap` builder stub only |
| `add_key` / `connect` (M3/M5) | NULL in `cfg80211_ops` | WPA2 hostapd stops at key install; wpa_supplicant stops at connect |
| `allow_frames` module param | **0 (fail-closed) by default** | even a complete provider sends nothing unless the bench raises the budget explicitly |

So the ladder has two modes:

* **Mode N (negative control, available now):** load and run the userspace
  commands; assert each one fails cleanly with `-EOPNOTSUPP` and that no frame
  was posted (`allow_frames` stayed 0 / `posted=0`), no device write happened.
* **Mode P (provider experiment, only after the builders land and a provider
  adapter is compiled in):** raise `allow_frames` in a supervised session and
  walk the message-level sequence. Stop rules in sec 6 apply.

## 1. Preconditions (all required before any step)

1. Attended bench session, recovery door READY (`tools/recovery-card.sh`), bench
   link PASS (`tools/bench-link.sh`); no remote/autonomous fire.
2. The cfg80211 image (kernel slice 13e81214..., STATUS.md sec 3k) boots on
   pair B with the clean-room driver; `cfg80211.ko` and `hi5622_wifi.ko`
   present on the box (paths per
   `opensource/docs/incoming/wifi-upstream-target/CFG80211-BENCH-IMAGE.md`).
3. The PD4-patched research firmware + local Wi-Fi block are staged exactly as
   in `opensource/docs/incoming/wifi-h2d-send/BENCH-PROCEDURE.md` (they are
   local bench inputs, never committed).
4. `iw` available on the box (stock iw pushed to /tmp is enough; the packaged
   iw/libnl-tiny is the img-integ lane's deliverable).
5. UART capture started read-only (`tools/uart-capture.sh start`); console log
   path absolute.
6. **One session = one idea.** After the 0x5A5A release no `rmmod` (project
   stop rule); a failure ends the session and the box is power-cycled.

## 2. Load + boot the clean-room driver (inherited sequence, abridged)

The H2D/context-create ladder must be run first: **hi5622_frame_post() only
accepts frames when the core phase is ACTIVE**, i.e. after ctx-create + TXQ
sync. Exact syntax and stop rules are in
`opensource/docs/incoming/wifi-h2d-send/BENCH-PROCEDURE.md`; the sequence is:

```sh
# C0: probe only (no device writes)
insmod /tmp/cfg80211.ko
insmod /tmp/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin \
        wifi_block_path=<local block> h2d_send_budget=3 allow_frames=0
D=/sys/devices/platform/0000:00:00.0    # check `ls`; PCI domain 0 = 2.4 GHz

# C1..C6 (from the h2d-send procedure; each step must report OK before the next)
echo 1 > $D/prepare ; echo 1 > $D/upload ; echo 1 > $D/verify
echo 1 > $D/seed ; echo 1 > $D/hcc_arm ; echo 1 > $D/hcc_loop
echo 1 > $D/release            # 0x5A5A only; NEVER write 0xCECE here

# C7/C8: ordered g0 id1 pair (see the h2d-send procedure C9..C13):
cat $D/h2d_plan $D/h2d_status
printf 1 > $D/h2d_ctx_create     # once
printf 1 > $D/h2d_txq_sync72     # once; core phase becomes ACTIVE
cat $D/h2d_status               # assert: phase=ACTIVE frames_sent=2 ok=2
```

Then the wiphy registration is gated on the ready handshake and happens
automatically (`cfg80211_auto=1` default). Assert:

```sh
dmesg | grep -E 'wiphy registered|VAP|scan layer'
# expect: "cfg80211: wiphy registered after the ready handshake ..."
ls /sys/class/ieee80211/phy0
iw phy phy0 info               # Wiphy phy0, 2.4 GHz channels 1..13
iw phy phy0 interface add wlan0 type managed   # M0/M1 netdev gate
ip link set wlan0 up
cat $D/cfg80211_status         # NEW: provider=- caps=none, frame_post allow_frames=0, scan: phase=idle
```

**STOP** if `cfg80211_status` is absent (wrong module build) or
`allow_frames` is not 0 (someone raised the budget before an approved
session). Record `sha256sum` of the loaded `.ko`.

## 3. Scan ladder

### 3.1 Mode N - expected clean `-EOPNOTSUPP`

```sh
iw dev wlan0 scan
echo "rc=$?"                   # expect non-zero; iw prints
                               #   "command failed: Operation not supported (-95)"
```

Expected dmesg (exactly one warning per op, ratelimited):

```
cfg80211: scan needs an H2D frame provider and none is registered
          - -EOPNOTSUPP (the g0 id0xC/id7/id8/WAL payloads are still [U]; ...)
```

Assertions: `cat $D/cfg80211_status` shows `provider=-`, `posted=0`,
`scan: phase=idle results=0/256`; no "frame post" dmesg line; UART shows no
firmware exception frame; golden page/state (0x86807FEF/.../state=1 mask=0)
unchanged. This proves the op fails closed and touched nothing.

Also negative-control the abort path:

```sh
iw dev wlan0 scan abort        # expect "command failed: Operation not supported (-95)"
```

### 3.2 Mode P - only with a provider adapter compiled in AND `allow_frames>0`

Preflight *read-only* first:

```sh
cat $D/cfg80211_status
# expect: provider=<name> caps=scan_req,scan_parse,... ; frame_post allow_frames=N
# scan: phase=idle
```

Then (budget must cover the frame; start with 4):

```sh
insmod ... allow_frames=4 frame_gap_ms=100   # fresh load only; never raise mid-session
printf 1 > $D/h2d_ctx_create ; printf 1 > $D/h2d_txq_sync72
iw dev wlan0 scan
```

Expected observable chain: `cfg80211: scan request posted by provider ...`
dmesg -> `frame post 'scan_req': g0 id12 ... announce consumed after N polls`
-> zero or more `scan DONE ... results/RES g1 15` events -> exactly one
`cfg80211: scan DONE (...)` -> `iw` prints BSS list or "No scan results".

**Stop rules (Mode P):** stop and power-cycle on any of
* `frame post 'scan_req': REFUSED ...` (gate refusal is a stop, not a retry),
* `announce NOT consumed` / H2D lane closed,
* core phase leaves ACTIVE, firmware exception frame on UART, golden page or
  mask changes,
* no `scan DONE` within the 5 s bounded timeout (the work item will report
  `aborted=1`; that is a recorded result, not a pass),
* the first parse error in a scan-result event (payload [U] - do not widen the
  decoder by guessing offsets).

## 4. hostapd ladder (AP)

### 4.1 Mode N - OPEN AP (positive op wiring, expected provider refusal)

```sh
cp hostapd-open.conf /tmp/hostapd-open.conf
hostapd -dd -t /tmp/hostapd-open.conf    # foreground, verbose
```

Expected: hostapd reaches `NL80211_CMD_NEW_BEACON` (`start_ap`), the driver
returns `-EOPNOTSUPP` (no provider), hostapd exits with a start_ap failure
mentioning `-95`/`Operation not supported` (capture the exact line - it is
the assertion), dmesg shows the `start_ap needs an H2D frame provider` warning,
`posted=0`, and the netdev remains usable (`ip link`, delete/recreate VAP).

### 4.2 Mode P - OPEN AP with a provider (design path, not yet available)

With `allow_frames>0` and a provider carrying `ap_start`:
`start_ap` fills the neutral `struct hi5622_ap_start_params` (SSID, chandef,
beacon interval/DTIM, privacy/auth, crypto summary, beacon head/tail bounded to
512/256 B), the provider builds the WAL group-2 frame, `hi5622_frame_post()`
gates + posts it. Expected dmesg: `cfg80211: start_ap handed to provider ... -
EXPERIMENTAL: beacons/keys remain firmware/M3 work`. hostapd then continues to
key setup and stops at `add_key` (`-EOPNOTSUPP`, M3) - **expected**.

### 4.3 Mode N - WPA2 AP (negative until M3)

```sh
cp hostapd.conf /tmp/hostapd.conf       # replace the placeholder PSK locally
hostapd -dd -t /tmp/hostapd.conf
```

Expected today: same `start_ap` `-EOPNOTSUPP` (no provider). With a provider
and an open gate only: `start_ap` may return 0, then hostapd fails at key
installation (`add_key` NULL => -EOPNOTSUPP) or at beacon/AP validation.
**Do not treat a start_ap success as an AP milestone** until keys, station
events and the data path exist (M3/M4); record which step failed.

## 5. wpa_supplicant ladder (STA)

```sh
iw dev wlan0 del
iw phy phy0 interface add wlan0 type managed
ip link set wlan0 up
iw dev wlan0 scan                      # prerequisite; today -EOPNOTSUPP
cp wpa_supplicant.conf /tmp/wpa_supplicant.conf   # replace placeholder PSK locally
wpa_supplicant -dd -i wlan0 -c /tmp/wpa_supplicant.conf -D nl80211
```

Expected today: wpa_supplicant fails at the scan prerequisite (or, if scans
are later made to work, at `NL80211_CMD_CONNECT` with `-EOPNOTSUPP`, because
the connect family is M5/NULL). Capture the exact error string; do not start
hostapd on the same box to work around it, and never leave wpa_supplicant in a
retry loop.

**STOP** on any kernel oops/WARN, UART exception frame, golden-page/mask
change, or if the driver logs `frame post ... AFTER the wire write` (a mailbox
desync: record producer/out[0], power-cycle, do not retry).

## 6. Recording the result

For each step record: command, returncode/exit code, exact iw/hostapd/wpa_supplicant
stderr line, the three dmesg lines (provider warning / frame post / scan DONE),
`cat $D/cfg80211_status` before and after, `h2d_status` counters
(`frames_sent`, `ok`, `timeout`, `refused`, `desync`), UART tail, and the
loaded module sha256. A hardware session is evidence only with those artifacts
attached; the lane's host-only claims stop at the M0/M1 gates.

## 7. What would make this ladder a REAL pass

1. wifi-vap-scan (task-110) delivers the g0 id0xC / g0 id7/id8 / g0 id9 /
   WAL AP builders with a host-tested provider adapter.
2. A bench session loads that adapter, raises `allow_frames` deliberately,
   and the firmware consumes the scan request.
3. cfg80211 BSS entries appear in `iw scan` and a scan completes with
   `aborted=0` (or a recorded error reason) at least once.
4. hostapd reaches start_ap with a firmware-acknowledged WAL frame; keys,
   station events and the data path are separate M3/M4 milestones.
