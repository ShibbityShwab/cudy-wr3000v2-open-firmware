# provider - AP-start provider extension patch

[apstart-provider.patch](apstart-provider.patch) applies on top of the
**hardware-fired provider base**

    opensource/docs/incoming/wifi-scan-complete/src

(module 748,888 B sha256 b42ff4ec..., the D2 TRUE PASS provider; it already
contains the wifi-vap-start provider integration).  It is NOT meant for the
older wifi-scan-ap tree any more - v2 was built from there and its C2 ladder
failed on the bench, so v3 pins the fired base.

Apply order:

    cp -a opensource/docs/incoming/wifi-scan-complete/src /tmp/ap
    cp opensource/docs/incoming/wifi-ap-start/src/hi5622_ap_frames.{h,c} /tmp/ap/
    ( cd /tmp/ap && patch -p1 < ../../wifi-ap-start/provider/apstart-provider.patch )

(also done automatically by ../tools/build-bench-module.sh and
../tools/kernel-check.sh, which verify the H2D/core files stay byte-identical
to the fired base)

What the patch changes (13 hunks):

| file | change |
| --- | --- |
| Kbuild | add hi5622_ap_frames.o |
| hi5622_frames_api.h | AP-start sequence fields on struct hi5622_ap_start_params (frame_index/band/module/vap_id/hidden_ssid/auth_algs), HI5622_AP_START_MAX_FRAMES, build_ap_start sequence contract comment |
| hi5622_scan.c | fill the new hints from cfg80211 settings; hi5622_scan_op_start_ap posts the whole provider sequence through hi5622_frame_post() (labels ap_ssid/ap_auth/ap_cfg/ap_vap_start), aborts on the first refusal, refuses zero frames |
| hi5622_vapscan_provider.c | include hi5622_ap_frames.h; AP-start builder callback (ap_start_mode 0 = g0 id1 static keys default, 1 = WAL g2 vendor order); advertise caps AP_START and register build_ap_start |
| hi5622_cfg80211_ops.c | hostapd gates: fail-closed .connect op (cfg80211 advertises NL80211_CMD_CONNECT so hostapd's nl80211 init gate passes; the op is never called by the AP path); corrected change_virtual_intf for SET_INTERFACE - accepts STATION<->AP<->MONITOR, stores dev->ieee80211_ptr->iftype (the cfg80211_change_iface WARN contract), updates the monitor ARPHRD and treats params->use_4addr == -1 as "not specified"; fail-closed .mgmt_tx op so cfg80211 accepts hostapd's AP-mode management-frame subscriptions (nl80211_register_mgmt refuses REGISTER_FRAME without it) |

Behaviour:

* default ap_start_mode=0 posts [ap_ssid (g0 id1 key 0x0005, 64 B),
  ap_cfg (g0 id1 key 0x0127, 184 B)] - statically dispatched by firmware
  table A (bench-proven carrier);
* ap_start_mode=1 posts the vendor WAL order [ap_ssid (68 B), ap_auth
  (36 B), ap_cfg (188 B), ap_vap_start (44 B)] - needs the dynamically
  registered group-2 handler [U];
* every frame is validated and posted by the existing fail-closed
  hi5622_frame_post() gate: allow_frames=0 refuses before any device write,
  and the per-load budget bounds the sequence (8 is enough for the ladder).

Verified with ../tools/build-bench-module.sh against the real 6.18.60 tree
(v4): hi5622_wifi.ko **204,712 B sha256
b5050518d44cb78601e86ebdd0d8a7f7ef4dc2ec7d26725abfe918fb2edc1c7e**
(debug-stripped, path-independent), depends=cfg80211, 14 haf_ symbols,
vapscan_ap_start + hi5622_cfg11_connect present, zero warnings; the H2D/core
files are byte-identical to the fired base.  The module is local evidence only
(not committed).  Hostapd flow and expected logs: ../HOSTAPD-BRINGUP.md.
