# provider - AP-start provider extension patch

[apstart-provider.patch](apstart-provider.patch) extends the wifi-scan-ap
frame-provider tree **after** the wifi-vap-start provider patch has been
applied (that is the module base the bench already runs in mode 1).

Apply order:

    cp -a opensource/docs/incoming/wifi-scan-ap/src /tmp/ap
    cp opensource/docs/incoming/wifi-vap-start/src/vapstart_frames.{h,c} /tmp/ap/
    cp opensource/docs/incoming/wifi-ap-start/src/hi5622_ap_frames.{h,c} /tmp/ap/
    ( cd /tmp/ap && patch -p1 < ../../wifi-vap-start/provider/vapstart-provider.patch )
    ( cd /tmp/ap && patch -p1 < ../../wifi-ap-start/provider/apstart-provider.patch )

What the patch changes:

| file | change |
| --- | --- |
| Kbuild | add hi5622_ap_frames.o |
| hi5622_frames_api.h | AP-start sequence fields on struct hi5622_ap_start_params (frame_index/band/module/vap_id/hidden_ssid/auth_algs), HI5622_AP_START_MAX_FRAMES, build_ap_start sequence contract comment |
| hi5622_scan.c | fill the new hints from cfg80211 settings; hi5622_scan_op_start_ap posts the whole provider sequence through hi5622_frame_post() (labels ap_ssid/ap_auth/ap_cfg/ap_vap_start), aborts on the first refusal, refuses zero frames |
| hi5622_vapscan_provider.c | include hi5622_ap_frames.h; AP-start builder callback (ap_start_mode 0 = g0 id1 static keys, 1 = WAL g2 vendor order); advertise caps AP_START and register build_ap_start |
| hi5622_cfg80211_ops.c | v2 hostapd gates: fail-closed .connect op (cfg80211 advertises NL80211_CMD_CONNECT so hostapd's nl80211 init gate passes; the op is never called by the AP path) and STATION<->AP<->MONITOR iftype changes accepted by the M1 netdev model |

Behaviour:

* default ap_start_mode=0 posts [ap_ssid (g0 id1 key 0x0005, 64 B),
  ap_cfg (g0 id1 key 0x0127, 184 B)] - statically dispatched by firmware
  table A (bench-proven carrier);
* ap_start_mode=1 posts the vendor WAL order [ap_ssid (68 B), ap_auth
  (36 B), ap_cfg (188 B), ap_vap_start (44 B)] - needs the dynamically
  registered group-2 handler [U];
* every frame is validated and posted by the existing fail-closed
  hi5622_frame_post() gate: allow_frames=0 refuses before any device write,
  and the per-load budget bounds the sequence (6 is enough for the ladder).

Verified with ../tools/kernel-check.sh and ../tools/build-bench-module.sh
against the real 6.18.60 tree (v2): hi5622_wifi.ko **199,344 B sha256
f9175ee3d2143429ce071ddce4103c0c8eabd14a173841ccdb253ecfb0754dc5**,
depends=cfg80211, 14 haf_ symbols, vapscan_ap_start + hi5622_cfg11_connect
present, zero warnings.  The module is local evidence only (not committed).
Hostapd flow and expected logs: ../HOSTAPD-BRINGUP.md.
