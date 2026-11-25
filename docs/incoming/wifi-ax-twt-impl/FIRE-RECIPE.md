# FIRE-RECIPE - the TWT setup path (TWT enable on the 2.4 GHz HE AP)

**Lane:** wifi-ax-twt-impl (shared task **task-199**).  **This lane never
fires.**  The Lead fires with the resident bench procedure (bench link PASS,
recovery card READY, human at the bench, UART capture read-only).  One change
per fire, one rung per fresh boot, no in-place retry, one load.

**Module under test:** the host-built `hi5622_wifi.ko` of BUILD-NOTES.md /
evidence/build.txt.  Installed form (OpenWrt `strip-kmod.sh` NO_RENAME=1,
what a fire deploys): **362,540 B** sha256
`ed13145a99a4b5dacc3b043abccddec333d51e8d129410c1c869d94544c01f93`,
**65 params** (merged 64 + `twt`), `.text` sha256
`1d7d6ddae70a9044c917381c52a124fed6adf6546489fece2b0ab6de9f5eb8e4`.
The same module is produced by `tools/build-twt-impl.sh`, which pins both
identities; if the deployed file does not match, STOP (wrong build).

**The question this fire answers:** does the firmware consume a **29-byte
g0-id1 config event, key 0x035D, body `01`** (the vendor TWT-enable path) on
the live 2.4 GHz HE AP and write the per-module context bits
**`ctx+0xDA.bit6 = 1`** (`0x40`; twt=3 adds bit7 -> `0xc0`)?  The
host-side TWT-enable half is closed iff the frame is posted with field B =
the **derived** AP-start module context and the independent read-only probe
reads the expected `ctx+0xDA` bits.  The **agreement** half needs an
associated STA and is explicitly conditional on the RX gate (H7).

Inherit **all** non-negotiables from ../wifi-ax-he-caps/FIRE-RECIPE.md and
../wifi-5ghz-beacon/FIRE-RECIPE.md: recovery armed, UART read-only, bench
link PASS, never write CA 0x400392f0, never read the RC misc window
0x10161000..0x1016ffff, never rmmod after the CPU release, never touch
endpoint 0001:00:00.0, page/state/mask anomaly -> power-cycle.

---

## H0 - host preconditions (read-only)

    uname -r                                   # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect ed13145a99a4b5dacc3b043abccddec333d51e8d129410c1c869d94544c01f93
    stat -c %s /lib/modules/HI_RELEASE/hi5622_wifi.ko     # 362540
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 65
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^twt:'    # (uint)
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status          # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/size/param count is not this lane's artifact (wrong
build) or the chip state is not healthy.  A fresh power-cycle before the load
is preferred; a new module load refuses to re-run the boot ladder on a chip
booted at probe.

## H1 - load with twt=1 (2.4 GHz HE profile, ONE load, no retry)

Same proven parameter set as the HE-AP fire, with every new gate explicit;
**twt=1** is the only new variable.

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        band5g=0 vap5g=0 ap_chan_wait_ms=0 desc_table=0 dfs_mode=0 \
        ba_enable=0 he_caps=1 twt=1

This rung loads the module **manually** (a bench fire), not through the
IMAGE #3 loader policy: that policy carries the 64-param list and would leave
`twt` at its fail-closed 0.  An image bake must add `twt` to the loader /
modprobe policy first (out of scope here; the same one-variable rule applies).

Verify the knobs (all):

    for p in band5g vap5g ap_chan_wait_ms beacon_static beacon_vap_start \
             he_caps twt desc_table dfs_mode ba_enable; do
        printf '%s=' $p; cat /sys/module/hi5622_wifi/parameters/$p; done
        # 0 0 0 1 1 1 1 0 0 0

STOP if any value differs.  Expected dmesg at probe:

    cfg80211: HE (802.11ax) caps advertised on 2.4GHz (AP iftype): ... 
    - hostapd ieee80211ax=1 gate unlocked

## H2 - boot ladder on endpoint 0000 ONLY (proven order)

    D=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $D/prepare
    echo 1 > $D/upload
    echo 1 > $D/verify
    echo 1 > $D/seed
    echo 1 > $D/hcc_arm
    echo 1 > $D/hcc_loop
    echo 1 > $D/release          # 0x5A5A only; NEVER 0xCECE here

Expected: all rc=0; page 0x86807FEF/7/2/0xABCD; ready handshake decoded;
`cfg80211: wiphy registered ... (1 band(s): 2.4GHz ...)`.

## H3 - wiphy/status assertions before the AP (read-only)

    ls /sys/class/ieee80211/                       # exactly phy0
    iw phy phy0 info | sed -n '/HE Iftypes/,+12p'  # HE AP block present
    cat $D/cfg80211_status | grep '^twt:'
        # twt: param=1 (enable) he_caps=1 beacon_static=1 last_rc=-61 posted=0 refused=0

## H4 - C2 ladder, interface, HE AP start (the host-side core rung)

    echo 3 > $D/h2d_ctx_create
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status                   # phase=ACTIVE ok=2 timeout=0

    iw phy phy0 interface add wlan0 type __ap
    ip link set wlan0 up

    /usr/sbin/hostapd -dd -t /tmp/hostapd-2g-he.conf 2>&1 \
        | tee /tmp/hostapd-2g-he.log

Config: `../wifi-ax-he-caps/ladders/hostapd-2g-he.conf` - open 2.4 GHz AP,
hw_mode=g, channel=6, ieee80211n=1, **ieee80211ax=1**, beacon_int 100,
DTIM 2.

**Expected dmesg, in this order (all of):**

```
cfg80211: start_ap beacon_static=1 plan (6 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start twt
frame post 'vap_add': ...
cfg80211: start_ap beacon_static: VAP-object readback=...
frame post 'ap_chan': ...            (0x0124 channel config)
frame post 'ap_ssid': ...            (0x0005)
frame post 'ap_cfg': ...             (0x0127)
cfg80211: start_ap beacon_static: 'ap_vap_start' posted after ap_cfg (channel=6) - vendor AP-start order complete
frame post 'twt': g0 id1 total 29 B ... handler risk=...; posted N/32
cfg80211: start_ap beacon_static: 'twt' posted (29 B, g0 id1 key 0x035D, flags=1 enable, field_b=0 = the AP-start module context 0x00119758)
cfg80211: twt post: module ctx=0x00119758 (field B 0) ctx+0xDA=0x40 expected bits 6/7=0x40 after 0 ms (TWT-enable readback MATCHES)
```

and hostapd prints **`wlan0: AP-ENABLED`** with **no** "Driver does not
support HE".

PASS (host side, all of):

1. the plan line lists `twt` **last**, after `ap_vap_start`;
2. the `'twt' posted` line reports **29 B**, key 0x035D, flags 1, and
   `field_b=0 = the AP-start module context 0x00119758` (the derived value,
   not a constant: if the live context differs, the line must show the
   derived field B and the matching context CA - record it);
3. the read-back line reports ctx+0xDA bits 6/7 = **0x40** (`MATCHES`) -
   diagnostic; the probe in H6 is decisive;
4. the sysfs line shows `param=1 ... posted=1 refused=0` and
   `last_rc=0`;
5. page 0x86807FEF, state 1, mask 0; no firmware exception.

FAIL (host side): no `twt` in the plan (twt not 1 - wrong load) / no
`'twt' posted` line (an earlier frame failed - follow that failure's rule) /
a REFUSED line (record the exact code: `-22` = parameter/AP-shape,
`-95` = band, `-11` = the module context did not resolve - that last one is
the F2 field-B class, hand back, never hard-code the field).

## H5 - the read-only probe (the decisive TWT-enable check)

Use the committed read helper (bar0read, physical addresses, O_RDONLY +
PROT_READ; refuses unaligned addresses and the RC misc window).  The chain is
the sibling lanes' (../wifi-ax-advanced T11):
`phys = 0x40000000 + CPU + 0x6B8000`.

| # | object | read | PASS |
| ---: | --- | --- | --- |
| 1 | **u8 ctx0+0xDA** | **CPU 0x119832** | **bits 6/7 == 0x40** (twt=1; = 0xc0 for twt=3); all other bits are pre-existing context state - mask `0xc0` |
| 2 | u32 ctx0+0x190 (VAP ptr) | CPU 0x1198E8 | contextual (may be 0 right after a fresh base post - bench R2 race, non-gating) |
| 3 | module table slot0 / base | CPU 0x171458 / u16 0x172504 | slot0 = **0x00119758**, base = 0x0000 [I] -> field B 0 is the AP-start module context (contextual proof of H4.2) |
| 4 | page/state/mask | sysfs | golden |

**HOST-SIDE TWT ENABLE PASS** iff probe 1 == 0x40 (mask 0xc0) and H4's five
gates hold.  If probe 1 keeps bit6 = 0: the firmware did not consume the
frame - failure modes below, do not re-post.

## H6 - the TWT client probe (CONDITIONAL on the RX gate; no on-air claim)

The per-user TWT **agreement** is initiated by a TWT-capable STA and handled
by the firmware D2H event `hdpp_d2h_user_twt_syn` (`{u16 uid; u8 flags}`,
flags bit0 -> `user+0x1C8.bit0`; ../wifi-ax-advanced T5).  It requires an
**association**, which the RX gate still blocks (the live-client blocker:
the MAC receives but host frames = 0).  Therefore:

* **RX gate open** (an associated STA appears: `iw dev wlan0 station dump`
  non-empty / the sibling RX lane has passed): have a **TWT-capable client**
  (a phone/laptop whose driver supports TWT; request a TWT agreement with the
  AP - the AP is TWT-enabled by this rung) associate to
  `cudy-ap-test`.  Evidence: the D2H TWT-sync event in the host RX trace
  (sibling sampler) and/or `user+0x1C8.bit0` via the read-only probe chain;
  record it as the AG-1 evidence pack.  This lane implements no agreement
  handler - a PASS here is evidence for the next lane, not for this one.
* **RX gate closed** (today): the client probe is **DECLINED/SKIP** - record
  "no association possible = no TWT agreement testable"; the rung's verdict
  is **host-side TWT enable only**.  Do not claim an on-air TWT result.

## H7 - negative controls (separate fires; one load per boot)

    # twt=2 must refuse BEFORE any frame:
    insmod ... twt=2 ...
        # dmesg: start_ap REFUSED - twt=2 (invalid) is not usable ... (-22); no frame sent
        # (the drift is already rejected at the first start_ap, not at load)
    # twt=1 without he_caps:
    insmod ... he_caps=0 twt=1 ...
        # dmesg: ... (-22) ... he_caps=1 ...
    # twt=1 without the vendor order:
    insmod ... beacon_static=0 twt=1 ...
        # dmesg: ... (-22) ...
    # twt=0 control (all else equal): no 'twt' line at all, plan has no twt
    # step, and probe 1 keeps bit6 = 0 - proves the 0x035D frame is the only change

(Each retry needs a fresh power-cycle: the module cannot be unloaded after
the CPU release.)

## Abort rules (inherit)

page not 0x86807FEF / state != 1 / mask != 0 -> power-cycle, record, no retry
in place; never write CA 0x400392f0; never read the RC misc window; no fire
without bench link PASS + recovery card READY + human present; record the
probe output under `build/linux-kbuild/waves/<phase>/` (gitignored).

## What to record on a PASS

module sha256/size/param count (H0); the H1 knobs readback; the HE
advertisement line; the four boot-ladder lines; the **complete start_ap plan
line with `twt` last**; the `'twt' posted` line (29 B, key 0x035D, flags,
field_b + live context CA); the `twt post` read-back line; the sysfs
`twt:` line; probe 1 = 0x40/0xc0 + probe 3; hostapd AP-ENABLED without the
HE refusal; page/mask golden; UART capture path.  That set is the TWT-enable
evidence pack (the agreement half stays open unless H6 ran with the RX gate
open).

## Why one variable

The only change vs the IMAGE #3 merged module is the added 29-B 0x035D
frame (plus its fail-closed parameter, the plan step, the read-back and the
sysfs line).  The F2 5 GHz chain, the RX watermark, the BA skeleton, the HE
advertisement and the 2.4 GHz AP-start ladder are unchanged, so a TWT verdict
or a re-rank is unambiguous.
