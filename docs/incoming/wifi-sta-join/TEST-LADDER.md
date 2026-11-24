# TEST LADDER - wifi-sta-join (task-146)

Host rungs run here (H0-H2, all PASS - see `evidence/`). The bench rungs
B0-B4 are **designed, not fired** by this lane: the host-only rule forbids
bench-device contact, and the Lead fires. Every bench rung is fail-closed:
one idea per boot, **one join per module load**, no retry, stop on the first
failed gate.

---

## 1. Test topology (external AP - the DUT is the STA)

The DUT is the Cudy WR3000 v2 running the bench image (our clean-room
module). It is **not** the AP in this ladder: it joins an **external AP**
over the air. No bench-router contact is needed; the bench router remains
available for recovery/SSH only.

```
   external AP (open SSID first)          DUT: Cudy WR3000 v2, bench image
   +--------------------------+           +--------------------------------+
   | A) host phone hotspot    |  2.4 GHz  | hi5622_wifi.ko sta_join=N      |
   |    "slowphone" (ch 6)    |<--------->|   wlan? managed (STA)          |
   | B) second AP / spare     |  802.11   | scan R4 fires first, then      |
   |    router, open, ch 6    |           |   iw connect <ssid> <bssid>    |
   +--------------------------+           +---------------+----------------+
                                                          | LAN/SSH (host)
                                                          v
                                            dmesg / cfg80211_status / page,
                                            mask, staging read pack
```

**AP choice**

* Option A - host phone hotspot (`slowphone`): 2.4 GHz, ch 6, **open**
  (no password) for B1/B2; keep it isolated (no internet needed). Phone
  hotspots that cannot do open networks are not suitable for B2 - use
  option B instead. BSSID: from the phone's hotspot info, or from the DUT's
  own scan (`iw dev sta0 scan`), whichever the operator trusts.
* Option B - a second AP/spare router: open SSID, 2.4 GHz ch 6 (or ch 1/11),
  beaconing, no WPA. This is the preferred option: stable BSSID, no phone
  UI auto-off.
* **WPA2 is deliberately NOT the first rung**: the 4-way/key + MAC RX paths
  belong to the wpa-static/rx-static lanes; the ladder first isolates the
  join post itself with an open network. A WPA2 rung is listed as B4-opt.

**Preconditions for every bench rung**

1. Fresh golden boot; the Lead's read pack: page `CA 0x001bf000`
   `0x86807fef/7/2/0xabcd`, state `CA 0x00118b60 = 1`,
   **mask `CA 0x00400008 = 0`, staging `CA 0x0040000c = 0`**.
2. External AP beaconing (verify with a phone/laptop, not with the DUT).
3. One module load; `allow_frames` explicitly set; no earlier aborted
   sequence in this load.
4. SSH/console capture ready: `dmesg -n 8`, `dmesg -c >/dev/null` marker.
5. UART capture is read-only if used (`tools/uart-capture.sh`); never type
   into the device UART.

---

## 2. Host rungs (run here; no device)

| rung | command | pass criterion | evidence |
| --- | --- | --- | --- |
| H0 | `./tools/selftest.sh` | `STA-JOIN-SELFTEST-PASS` (111 checks: bounds, gates, body/address policy, 140/48-B frames, D2H decode, capability constants) | `evidence/selftest.txt` |
| H1 | `tools/fw_sta_join_re.py <local vendor .ko>` | `STA-JOIN-RE-PASS` (86 checks: opcode/body chain, 108-B field map, D2H dispatch table, completion handlers) | `evidence/sta-join-re-2.5.24.txt` |
| H2 | `./tools/build-sta-join-hardening.sh` | patch applies on all-patch integration tree, zero-warning 6.18.60 build, strip preserves `.text`, 47 params, feature strings, selftests | `evidence/build-verify.txt`, `build.log`, `artifact-identity.txt` |
| H3 | safety audit (part of H0) | `STA-JOIN-AUDIT-PASS` (23 checks: no hexdump/no `sme->key` in logs, dry-run key gate, fail-closed gate evidence, no vendor binaries) | `evidence/selftest.txt` |

---

## 3. Bench rungs (Lead fires, one idea per boot)

Module load command (bench image path may differ):
`insmod /lib/modules/$(uname -r)/hi5622_wifi.ko sta_join=<0|1|2> allow_frames=<n>`.
Reload: `rmmod hi5622_wifi` then insmod (a fresh load is required for the
one-shot latch; `sta_join` is a 0444 read-only parameter).

Status/counter command used below:
`cat /sys/bus/pci/devices/0000:00:00.0/cfg80211_status` - it contains the
scan-layer line and the inherited `frame_post: allow_frames=... posted=...
refused=... timeout=... bad=...` counters.
STA interface: `iw phy phy0 interface add sta0 type managed` (or the
managed iface the image already creates).

### B0 - params + capabilities control (no join)

| item | value |
| --- | --- |
| load | defaults `sta_join=0 allow_frames=16` |
| action | `modinfo hi5622_wifi | grep sta_join`; `cat .../cfg80211_status`; `iw phy`; `iw dev`; `iw dev sta0 scan` once (the scan path is already proven; it also gives the external AP's BSSID) |
| pass | wiphy registers, baseline HT capabilities unchanged (patch changed no parameter default), scan returns the external AP, `frame_post: ... posted=0` |
| join check | `iw dev sta0 connect "slowphone" 2412 <bssid>` returns `-EOPNOTSUPP`; dmesg: "firmware join is disabled (sta_join=0 fail-closed)"; **no frame_post post, no out[0], page/mask/staging golden** |
| stop | any wiphy/scan/regression failure; any page/mask/staging change |

### B1 - dry run (build + validate, nothing posted)

| item | value |
| --- | --- |
| load | fresh boot: `sta_join=1 allow_frames=16` |
| action | scan for the external AP BSSID, then a **real** `iw dev sta0 connect "slowphone" 2412 <bssid>` |
| pass | dmesg: `"WAL cfg-event opcode 0x123 frame built+validated (140 B ... key NOT copied) - dry run (sta_join=1), nothing posted"`; userspace gets `-EOPNOTSUPP`; `frame_post: posted=0` and allow_frames unchanged; `out[0]` never written; page/mask/staging golden |
| stop | any frame post in dry run (must never happen); page/mask/staging change |

### B2 - single post (THE experiment: firmware deref contract [U])

| item | value |
| --- | --- |
| load | fresh boot: `sta_join=2 allow_frames=16` (allow_frames>0 required) |
| action | scan once (BSS entry), then **one** `iw dev sta0 connect "slowphone" 2412 <bssid>` |
| pass | dmesg: `"POSTING WAL cfg-event opcode 0x123 (140 B ...) - single shot, no retry"` followed by `"posted (bssid=...)"`; `frame_post: posted=1`; page golden, **mask 0, staging 0** during and after; ideally a `STA D2H g2 id3 assoc-completion` log-only line (status/bssid/lengths) - that is the firmware's answer |
| stop | **mask != 0 or staging != 0 or page != golden** -> STOP, power-cycle, capture UART/dmesg, do not retry (this is the [U] answer: the firmware did not accept the body addresses); also stop on any `frame post ... NOT posted` line (final for the load), or on a second post attempt |
| one-shot check | a second `iw connect` in the same load returns immediately; dmesg "already joined in this load - one join per load, no retry"; `posted` stays 1 |

### B3 - disconnect (0x00d0)

| item | value |
| --- | --- |
| load | same boot as a **successful** B2 (if B2 failed/stopped, skip B3 - fresh boot later) |
| action | `iw dev sta0 disconnect` (or ifdown) |
| pass | dmesg: 48-B `opcode 0xd` frame posted (`sta disconnect: ... posted ... join buffer freed`); page/mask/staging golden; a repeat disconnect returns `-ENOTCONN`/`-EALREADY` with no second post |
| stop | any post refusal (final), mask/staging change |

### B4 - repeat (fresh load, one post again)

| item | value |
| --- | --- |
| load | `rmmod hi5622_wifi`; fresh inspection (page/mask/staging golden); `insmod ... sta_join=2 allow_frames=16` |
| action | one connect as B2 (same external AP, re-scan first) |
| pass | exactly one post for this load; the new load's latch is fresh (this proves per-load semantics and that the artifact survives reload); page/mask/staging golden |
| gate control (optional, same boot) | a third load with `sta_join=2 allow_frames=0` -> `-EPERM` "join gate refused", **no post** |
| stop | same stop rules; if B2's answer was a mask/staging change, **do not run B4** - power-cycle and analyse |

### B4-opt - WPA2 AP (only after the wpa/rx lanes land)

Same as B2 but a WPA2-PSK external AP and `wpa_supplicant`; pass still
requires the join post + one-shot gates, and additionally observes the
WPA path. This lane does not design the key/4-way rungs (wpa-static lane).

---

## 4. Cross-cutting stop rules

| witness | stop rule |
| --- | --- |
| page `CA 0x001bf000` | any word != `0x86807fef/7/2/0xabcd` -> STOP |
| state `CA 0x00118b60` | != 1 -> STOP |
| **mask `CA 0x00400008`** | **!= 0 -> STOP** (driver refuses on the latched value too) |
| **staging `CA 0x0040000c`** | **!= 0 -> STOP** (driver refuses on any captured vector) |
| out[0] `CA 0x40039010` | not consumed after a post -> STOP, record producer/out[0], power-cycle |
| any `frame post ... NO retry` | final for the load; no second attempt |
| any UART panic/abort | capture, power-cycle, analyse offline |

One idea per boot: **a post is the experiment**. A failed B2 is a result,
not something to retry - the latch enforces this in the driver, and the
ladder's stop rules enforce it for the operator.

## 5. Evidence to capture per rung

```sh
dmesg > /tmp/$RUNG-dmesg.txt
dmesg | grep -E 'sta connect|sta disconnect|STA D2H|frame post' 	> /tmp/$RUNG-join.txt
cat /sys/bus/pci/devices/0000:00:00.0/cfg80211_status > /tmp/$RUNG-status.txt
cat /sys/module/hi5622_wifi/parameters/sta_join
# + the Lead's read pack: page / state / mask / staging before and after
```

Pass/fail summary to record: rung, load params, first post line, frame_post
counters, page/mask/staging before/after, D2H g2 id3/4 lines, UART state.
Store under the bench evidence tree (gitignored), not in this directory.

## 6. This lane's result

* Host/compile lane: **PASS** (H0-H3; see `evidence/`).
* Bench: **not fired**. B0-B4 above are the deliverable; the Lead's fire
  answers the one remaining [U] (firmware dereference contract of the
  four 108-byte-body addresses). Until a post is made, the driver keeps the
  path default-off.
