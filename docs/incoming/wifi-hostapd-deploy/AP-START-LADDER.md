# AP-START-LADDER - exact attended AP-start sequence

**Lane:** wifi-hostapd-deploy (task-125).  **Attended only.**  Host-only lane:
none of this was executed on the router by this lane.  This is the exact
command sequence for the Lead's next bench session; the machine-readable form
is [recipes/ap_start_ladder.json](recipes/ap_start_ladder.json) (validated by
`tools/check-ladder.py`, 107 checks).

Read with [DEPLOY.md](DEPLOY.md) (hostapd package path),
[../wifi-ap-start/REPORT.md](../wifi-ap-start/REPORT.md) (the AP-config RE this
ladder exercises) and [../wifi-vap-start/TABLES.md](../wifi-vap-start/TABLES.md)
(T18 readback convention).

## 0. Preconditions (all must hold)

* Pair-B boots the cfg80211 bench image (kernel slice `13e81214...`, slot
  `1794a531...`), golden cold-boot state reachable.
* PD4-patched vendor firmware available for the module's `fw_path`
  (928,920 B, sha256 `cfe24bfb...`) + the 2,472 B PHY block.
* The **AP-start provider module** built by
  `../wifi-ap-start/tools/kernel-check.sh` (770,304 B, sha256
  `115119809f103abe753c335e8a386c58987f2578422cc1d359db1d0303992f94`;
  wifi-scan-ap src + wifi-vap-start + wifi-ap-start provider patches).  It is
  local evidence, not committed; rebuild it if absent.
* `cfg80211.ko` from the same 6.18.60 tree (2,328,548 B, sha256
  `949393f9...`).
* hostapd package set installed by `./deploy-hostapd.sh --apply`
  (hostapd v2.12 + fixtures; verify with `--verify`).
* Bench link PASS, recovery door READY, read-only UART capture running
  (`tools/bench-link.sh`, `tools/recovery-card.sh`).
* Human at the bench.  No agent runs these steps.

## 1. Phase H0 - hostapd deploy verify (read-only)

    cd opensource/docs/incoming/wifi-hostapd-deploy
    ./deploy-hostapd.sh --verify --attended --yes-really \
        --evidence ../build/wifi-hostapd-deploy/ap-start/<stamp>-verify.txt

PASS: device preflight green; all four packages installed at the pinned
versions; 8 installed files with matching sha256; `hostapd -v` prints
`hostapd v2.12`; `/root/hostapd-deploy/hostapd-open.conf` present; no
`/etc/rc.d` wpad autostart link.

STOP on any FAIL: do not hand-run half a package set - fix with
[DEPLOY.md](DEPLOY.md) or roll back.

## 2. Phase B+C - boot ladder + six-message calibration (single session)

The AP-start module must be loaded for the whole session: C2, the VAP base and
the AP start all ride the same module instance and the frame budget is a
per-instance total.  Use the proven cfg80211 bench runner with the C2
sequence and the AP-start module parameters:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    cd ../wifi-bench-tooling
    python3 tools/bench_session.py --mode cfg80211 \
      --sequence tools/sequences/bringup-c2-calibration.json \
      --module <AP-START-MODULE>.ko \
      --cfg80211-ko <cfg80211.ko> --iw-bin <iw> --libnl-tiny <libnl-tiny.so> \
      --fw-file <FIRMWARE.PD4.bin> --fw-manifest <manifest.json> \
      --wifi-block-bin <wifi_block.bin> --ssh-host 192.168.1.1 \
      --module-param allow_frames=16 --module-param frame_gap_ms=100 \
      --module-param h2d_send_budget=8 --module-param vap_start_first=1 \
      --module-param vap_start_mode=1 --module-param vap_start_module=0 \
      --module-param ap_start_mode=0 \
      --yes-really --attended

PASS: boot ladder + strict ready pair; ctx_create type 1 + txq_sync72 + six
cfg_cal 3502..3507 all CONSUMED (`rd_catalog` g1 id3 count +6; mask 0;
`core phase=ACTIVE`); wiphy registered; module still loaded at the end.

**allow_frames budget:** C2 posts 8 frames, the iface-add user_add pair 2, the
scan VAP base 3 (vap_add + vap_start + scan_req) and the AP start 2 = 15; the
recipe uses 16.  A budget of 6-8 refuses the AP frames late in the ladder.

STOP on any gate failure: keep the C2 evidence and power-cycle; do not raise
the budget mid-session.

## 3. Phase V - interface type + mode-1 VAP base (raw, over the project ssh key)

All commands below run on the box (prefix with
`ROUTER_HOST=root@192.168.1.1 tools/rsh.sh` from the repository root, or use
the existing bench transport).  Read-only BAR0 reads use the bench helper
`/root/hi5622-bar0read` (static ARM /dev/mem mmap, READ-ONLY).

### V1 - create the VAP as AP type (critical)

    iw dev wlan0 del 2>/dev/null || true
    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up

PASS: rc=0; dmesg `cfg80211: VAP 'wlan0' added (type AP, ...)` and the
provider's user_add pair:
`frame post 'user_add': g0 id7 total 180 B ... consumed`,
`frame post 'user_add_notify': g0 id8 total 180 B ... consumed`.

Why `__ap`: hostapd v2.12 always sends NL80211_CMD_SET_INTERFACE(AP) during
driver init; our `change_virtual_intf` returns -EOPNOTSUPP for a real type
change, so a **managed** wlan0 aborts hostapd with
`nl80211: Could not configure driver mode` /
`nl80211 driver initialization failed` and NEW_BEACON is never sent.  With an
AP-type VAP the same SET_INTERFACE returns 0 (same type) and hostapd proceeds.
iw 6.17 maps the literal string `__ap` to NL80211_IFTYPE_AP; `ap` and
`managed` are wrong here.

### V2 - mode-1 VAP base (posted by ONE bounded scan)

    iw dev wlan0 scan freq 2412     # one channel; 2412 MHz = ch 1

PASS: the provider posts the mode-1 VAP base *inside the scan op*, in this
order: `frame post 'vap_add': g0 id1 total 48 B ... consumed` (key 0x64),
`frame post 'vap_start': g0 id1 total 40 B ... consumed` (key 0x65), then
scan_req g0 id0xC.  Read-only readback: module-table slot0
(BAR0 0x829458) = `0x00119758`, and ctx0+0x190 (BAR0 0x7D18E8) becomes
nonzero.

Notes: the scan itself may finish-abort at the host window - the scan result is
**not** a gate for the AP ladder (the firmware VAP object is).  Do not retry
the scan; the frames are already on the wire.

## 4. Phase A - hostapd start (the AP sequence)

### A1/A2 - start hostapd and read the firmware evidence

    hostapd -B -i wlan0 -P /root/hostapd-deploy/hostapd.pid \
        /root/hostapd-deploy/hostapd-open.conf
    echo "hostapd rc=$?"

PASS (host-side): rc=0.  `hostapd -B` daemonizes only after interface setup,
so rc=0 means the cfg80211 driver's `start_ap` returned 0.

PASS (driver-side dmesg, the decisive evidence):

    cfg80211: start_ap sequence frame 1/4 'ap_ssid' posted (64 B, ssid_len=..., freq=2412 MHz ...)
    frame post 'ap_ssid': g0 id1 total 64 B ... consumed ...
    cfg80211: start_ap sequence frame 2/4 'ap_cfg' posted (184 B, ... beacon=100 dtim=2 ...)
    frame post 'ap_cfg': g0 id1 total 184 B ... consumed ...
    cfg80211: start_ap sequence posted to provider 'hi5622_vapscan' (2 frame(s))

PASS (attribute gates): `cfg80211_status` contains
`frame_post: allow_frames=16 ... refused=0 timeout=0 bad=0`;
golden page/state unchanged; exception mask BAR0 0x6F8008 = 0; staging
BAR0 0x6F800C = 0.

The static g0 carrier posts **two** frames (`ap_ssid` key 0x0005, `ap_cfg`
key 0x0127 with the 156-byte body from wifi-ap-start); the provider loop stops
on the next builder call (-ENODATA).  The optional WAL group-2 rung
(`ap_start_mode=1`) posts four frames (0x0005/0x006D/0x0127/0x0065) and is
unverified - keep `ap_start_mode=0`.

### A3 - read-only post-start readback

    iw dev wlan0 info
    ip link show wlan0
    kill -0 "$(cat /root/hostapd-deploy/hostapd.pid)" && echo HOSTAPD_ALIVE

Then read the context flags and (if the pointer is sane) the VAP fields:

| read | BAR0 | expected |
| --- | --- | --- |
| golden page word0 | 0x877000 | 0x86807FEF |
| golden page +4/+8/+C | 0x877004/8/C | 0x7 / 0x2 / 0xABCD |
| boot state | 0x7D0B60 | 1 |
| exception mask / staging | 0x6F8008 / 0x6F800C | 0 / 0 |
| module-table slot0 | 0x829458 | 0x00119758 (if not: STOP, do not dereference) |
| ctx0 (CA 0x119758) | 0x7D1758 | context body base |
| ctx0+0x190 VAP pointer | 0x7D18E8 | nonzero after V2 |
| ctx0+0x18C flags word | 0x7D18E4 | **differs from the pre-A1 capture** (AP-config worker mode/HT flags) |
| VAP+0x10 beacon interval | u32(0x7D18E8) + 0x6B8000 + 0x10 | 100 (0x64) [I: deref window] |
| VAP+0x14 DTIM | u32(0x7D18E8) + 0x6B8000 + 0x14 | 2 [I] |

The non-dereference gates (ctx0+0x18C changed, mask/staging 0, page/state,
dmesg + attribute gates) are sufficient for a first AP-start PASS; the VAP
pointer dereference is the [I] confirmation and must be recorded either way.
`../tools/readback-pack.sh` prints this pack (and a `bar0read` command form).

Arithmetic note: ctx0 CA 0x119758 -> BAR0 **0x7D1758** (CA + 0x6B8000).  The
2026-11-26 13:00 ledger line "0x7D2758 = 0x119758+0x6B8000" is off by 0x1000;
the validated cross-check is ctx4: CA 0x11C458 -> BAR0 0x7D4458 and the D2
sequence's 0x7d45e8 = ctx4+0x190.  Recompute from the slot0 read on the box;
never trust a stale constant.

### A4 - optional WAL carrier rung (do not run before the static case passes)

Only on a fresh boot: reload the module with `ap_start_mode=1` and repeat H0
+ V1/V2 + A1/A2.  PASS = all four frames CONSUMED and the same readback
change.  If not, the WAL group-2 dynamic handler remains [U] and the static
g0 carrier stays the supported path.

## 5. Cleanup

    kill "$(cat /root/hostapd-deploy/hostapd.pid)" 2>/dev/null || true
    iw dev wlan0 del
    # package set stays installed for the next session; remove only with:
    ./deploy-hostapd.sh --rollback --attended --yes-really

## 6. Stop rules (fail-closed)

* exception mask (BAR0 0x6F8008) != 0 or staging (BAR0 0x6F800C) != 0;
* golden page/state or `g+0xB4/+0xB8` changes;
* module-table slot0 != 0x00119758 (do not dereference a guessed context);
* a posted frame is not CONSUMED (cfg80211_status refused/timeout/bad
  increments), any dmesg fault, the H2D lane closes, or hostapd rc != 0;
* never re-send start_ap or the VAP base in the same boot after a stop -
  power-cycle and analyse offline;
* never write CA 0x400392F0, never post mailbox id 5, never send group 1/4;
* never rmmod; never reboot except through the documented attended path.

Note on hostapd rc: if hostapd returns nonzero but the dmesg shows both AP
frames posted and CONSUMED and the readback changed, record it as a
driver-partial/unknown rather than PASS - do not retry in the same boot
(a second start_ap would re-run the firmware AP-config worker).

## 7. Claim rules

* a CONSUMED frame is not an AP: PASS(A2) requires the firmware readback
  (ctx0+0x18C change; VAP+0x10=100/+0x14=2 when the deref resolves);
* beaconing over the air is NOT observable from the same radio and is out of
  scope; hostapd rc=0 is a host-side fact only;
* do not claim a functional AP (association/data path): add_key, STA events and
  the data plane are separate M3/M4/M5 work.
