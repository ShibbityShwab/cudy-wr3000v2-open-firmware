# GATES - PASS gates and decision tables (wifi-bench-session)

Every gate is **fail-closed**: the first failed hard gate stops the phase
(`STOP:<step>`), the evidence report keeps every read line up to that point,
and the driver still issues the cleanup reboot. `PASS` is never inferred from
absence of evidence.

## 0. Global gates (every bench_session phase, before any sequence step)

| gate | source | expected |
| --- | --- | --- |
| bench link | `tools/bench-link.sh` (bench_session preflight) | rc 0, adapter UP + both addresses |
| recovery door | `tools/recovery-card.sh` | rc 0 = READY |
| UART capture | `build/uart-logs` live capture detected | read-only capture running |
| TFTP state | `tftp_server` process check | safe state |
| single session | no other `bench_session.py` running | none |
| attended | operator flag | `--attended` |
| evidence dir | project-gitignored path | writable |
| kernel | `uname -r` in the device script | 6.18.60 |
| vendor modules | `/proc/modules` | no `hi5622v100_*` vendor stack |
| module sha/size | device `sha256sum` vs pin | exact |
| firmware sha/size | device `sha256sum`/size vs pin | 928,920 B `cfe24bfb...` |
| PHY block | seed step word count/readback | 2,472 B, all words read back |
| cfg80211 order | `/proc/modules` | cfg80211 before hi5622_wifi |
| boot ladder | sysfs gates | windows=1, fw_loaded=1, dmesg diffs=0, hcc armed+loop, cpu_released=1 |
| golden page LATCH | rd32 BAR0 0x877000..0x87700C | 0x86807FEF / 0x7 / 0x2 / 0xABCD with +8==2 |
| boot state | rd32 BAR0 0x7D0B60 | 1 |
| exception mask / staging | rd32 BAR0 0x6F8008 / 0x6F800C | 0 / 0 |
| ready pair | `hcc_ready` | g4 id1 <= 2.0 s, g4 id2 <= 20.0 s, phase READY_TXQ_SYNC |
| wiphy registration (cfg80211 modes) | `/sys/class/ieee80211/phy*` | appears within 20 s of ready; `cfg80211_status` shows registered=1 |

Posture: the script audit in bench_session enforces the hard rules statically
(no CA 0x400392F0 write, no 0xCECE unless the +8==2 latch with cece_mode=1 by
default, release 0x5A5A only, one announce, devmem only in the custom-block
windows).

## 1. Phase S - 13-channel scan

Sequence: [sequences/session-scan13.json](sequences/session-scan13.json).

| gate | evidence | FAIL classification |
| --- | --- | --- |
| pre-create ctx empty | `ctx4+0x190 == 0`, mask/staging 0 | STOP (state not fresh) |
| ctx_create consumed | `h2d_status ctx_created=1` | STOP |
| created body | ctx4[0]=4, +0x10=0x00010001, +0x434=0x0100478C | STOP (A preconditions) |
| txq_sync72 consumed | `txq_sent=1`, mask/staging 0 | STOP |
| iface add | `iw phy phy0 interface add wlan0 type managed` rc 0 + netdev present | STOP |
| link up | `ip link set wlan0 up` rc 0 | STOP |
| VAP model | `cfg80211_status` contains `vap0: wlan0` | STOP |
| scan request | `iw dev wlan0 scan` rc 0 | STOP |
| per-channel results | dmesg has `scan channel result` lines, `scan DONE` present | STOP (no firmware result) |
| scan finished | `scan: phase=finished` + `scan finish: last=` present | STOP |
| firmware health | mask 0, staging 0, page golden/state 1 after the ladder | STOP (exception) |
| **full coverage** | `chans=13/13` **and** `scan finish: last=finish-ok`; absent `last=finish-abort` | **STOP** (partial scan is the experiment's failure branch, never PASS) |
| **results/bss** | `results=` and `bss=` non-zero (`results=0/`/`bss=0 ` absent) | STOP |
| **iw output** | host gate: report.txt contains >=1 `BSS ` line from `iw dev wlan0 scan` | STOP |
| provider wire | `allow_frames=16 refused=0 timeout=0 bad=0` | STOP |
| prerequisite H2D | `h2d_status timeout=0 refused=0` | STOP |
| teardown | `iw dev wlan0 del` rc 0 | recorded; not a verdict change |

Known constraint: `scan_timeout_ms=60000` is clamped by the module to 45,000
ms. The measured firmware pace is ~3.5 s/channel (13 ch ~= 45.5 s), so a
12/13 result at the clamp is the documented possible STOP; the fix is a
scan-complete lane change (higher clamp / split request), not a retry.

## 2. Phase A - AP start

### 2.1 Base session (sequences/session-ap-base.json)

All global gates plus:

| gate | evidence | FAIL |
| --- | --- | --- |
| ctx_create consumed | `ctx_created=1`, created body pack as S | STOP |
| txq_sync72 consumed | `txq_sent=1`, mask/staging 0 | STOP |
| core active | `h2d_status ok=2 timeout=0 refused=0` | STOP |
| pre-health pack | page/state golden, mask/staging 0, out0/out2 0 | STOP |

The base sequence deliberately creates no VAP and posts no AP frame; a base
STOP means the AP stage is **not** attempted (the box state is unknown).

### 2.2 AP device stage (device/ap-fire.sh + device/ap-readback.sh)

| gate | evidence | FAIL |
| --- | --- | --- |
| tools present | bar0read, iw, hostapd, config, module loaded, phy0 | STOP (deploy gap -> fix pins) |
| golden pre-pack | page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0, out0/out2 0 | STOP (do not fire hostapd) |
| iface type __ap | `iw dev` lists wlan0 after `interface add wlan0 type __ap` | STOP |
| link up | `ip link set wlan0 up` rc 0 | STOP |
| VAP base | dmesg `frame post 'vap_add'` **and** `frame post 'vap_start'` | STOP (mode-1 VAP not created; no AP attempt) |
| VAP pointer | ctx0 (BAR0 0x7D1758) -> ctx0+0x190 (0x7D18E8) nonzero | STOP (firmware VAP object missing) |
| hostapd start | `hostapd -B ...` rc 0 | STOP (start_ap didn't run; log captured) |
| AP frames | carrier set: mode 0 (g0 static) = `frame post 'ap_ssid'` + `'ap_cfg'` (dmeg 1/4, 2/4 of the max-4 slots); mode 1 (WAL g2) = all four labels | STOP |
| sequence posted | dmesg `start_ap sequence posted to provider`; no `not posted` | STOP (partial AP start aborted by design) |
| provider wire | `cfg80211_status refused=0 timeout=0 bad=0` | STOP |
| golden post-pack | page/state golden, mask/staging 0, out0/out2 0 | STOP (exception/health) |
| ctx flags change | ctx+0x18C/0x18D aligned word changes after ap_cfg | WARN if unchanged; readback path decides |
| **AP readback** | vap0+0x10 == 100 (beacon interval), vap0+0x14 == 2 (DTIM) | resolved mismatch = STOP; **unresolved pointer = PASS-PARTIAL** |

Classification: **PASS** = frames + clean packs + readback 100/2; **PARTIAL**
(frames consumed, readback deref unresolved - the ap-start lane marks the
vap0+0x18C-style deref [I]); **STOP** = any row above. A consumed frame alone
is not an AP: the report states which class was reached. Beaconing over the air
is out of scope (not observable from the same radio).

## 3. Phase X - first MAC data-plane TX/RX (merged provider+dp module)

Authoritative gate source: the wifi-dp-air lane's committed sequence
`../wifi-bench-tooling/tools/sequences/bringup-f-air-txrx.json` (30 steps /
83 checks / 7 writes), run through the same bench_session engine as S/A. The
driver adds the evidence extraction (posted/completion_pending/captures) and
reports the lane's verdict wording.

| gate | evidence | FAIL/outcome |
| --- | --- | --- |
| global gates + cfg80211 wiphy | section 0 | STOP |
| ctx_create / txq_sync72 | `ctx_created=1`, created body pack, `txq_sent=1` | STOP |
| iface add + link up + VAP model | `vap0: wlan0`, rc 0 | STOP |
| scan (VAP + channel precondition) | `iw dev wlan0 scan` rc 0, `scan: phase=finished`, `results=` non-zero; absent `results=0` | BLOCKED-DRIVER only with the lane's documented blocked signature; otherwise STOP |
| scan dmesg | `scan channel result` present | STOP |
| dp_probe | `dp_probe: MAC band0 CA 0x40040000` | STOP |
| dp_setup | `setup_done=1 failed=0 mac_regs=14 table_entries=8` | STOP |
| dp_rings | `+0x38=0x8000`, `+0x3c=0x8000`, `+0x48=0xfffffcc1`, tid 7 entry | STOP |
| pre-TX status | `bound=1 failed=0 posted=0` | STOP |
| 0x0320 hand-off | `txq_addr_sent=1 ok=3 timeout=0 frames_sent=3` | STOP |
| TX frame 1 | `posted=1`, `vap_type=1`, `MSDU table entry`, completion read | STOP on driver refusal / table mismatch |
| TX frame 2 | `posted=2` | STOP on refusal |
| RX window | `polled=1 captures=` + descriptor semantics note | never a failure (NO-RX is a recorded outcome) |
| post packs | mask/staging 0, state 1, page golden, out0/out2 0 | STOP |
| final h2d | `timeout=0 refused=0 txq_addr_sent=1` | STOP |
| verdict | COMPLETED (completion_pending=0) vs POST-ONLY (>0) vs NO-RX (captures=0) | recorded; on-air claim needs an external observer |

## 4. Status meanings and stop rules

| status | meaning | next |
| --- | --- | --- |
| PASS | every hard gate green | proceed to next phase |
| PARTIAL | frames/effects proven, one [I] readback unresolved (phase A only) | record; decide with the lane; do not claim an AP |
| BLOCKED-DRIVER | a documented pending-feature outcome (sequences mark it) | record; new lane work |
| STOP | a hard gate failed | no retry in the same boot; power-cycle; analyse the evidence offline |
| FAIL/ERROR | tooling/host failure (never a firmware verdict) | fix tooling; do not re-fire until green dry-run |
| DEPENDENCY-PENDING | a lane pin is not delivered | fill artifacts.env from the lane commit |

Universal stop rules: never re-send a frame after a stop; never reload the
module in the same boot; never write CA 0x400392F0 / RC misc window / mailbox
id 5 / group-1/4 frames; never rmmod; never type to the UART; a nonzero
mask/staging or a page/state drift is always STOP (never PASS/PARTIAL).