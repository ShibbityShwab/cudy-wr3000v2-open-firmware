# FIRE-RECIPE - the fail-closed BA core: smoke rungs now, session rungs after RX

**Lane:** wifi-ba-agg (task-178). **The Lead fires**; this lane never
contacted the router. Module under test: the host-built skeleton
`hi5622_wifi.ko` of `BUILD-NOTES.md` / `evidence/build-artifact.txt` -
debug-stripped **359,992 B** sha256
`784608410b4dd8e4c74be9b923d62465d4c397d12e84ee2ce58710efcc33de02`,
**58 params** (57 base + `ba_enable`, default **0**).

**This is one-shot per rung.** If a PASS/FAIL gate does not hold, STOP,
record, power-cycle. Do not reload the module twice, do not restart hostapd,
do not improvise a rung.

**Read this first - what the skeleton actually is.** The delivered patch adds
the decoded, host-tested BA core to the module **without wiring it into the
driver**: no object outside `hi5622_ba.o` references a `hi5622_ba_*` symbol
(asserted by the build script). Therefore:

* **B1/B2 are the rungs evaluable with this artifact today** - they prove the
  module still behaves exactly like the bench-proven base when the BA core is
  compiled in and armed, and that arming it does **not** emit BA traffic with
  no peer.
* **B3-B6 are the contract for the integrated module** (the next rung: wire
  the hooks in REPORT.md sec. 9). They are written now so the integration is
  a one-shot fire later. **Do not fire B3-B6 against the skeleton** - they
  would only re-measure the RX gate.
* The RX gate still stands: without the RX capture no client associates, so no
  BA session can be negotiated end-to-end. B3+ are BLOCKED on RX.

**Non-negotiable (inherit BENCH-FACTS / LESSONS / RUNBOOK):**

* recovery door armed, UART capture running READ-ONLY, bench link PASS; no
  human at the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read the
  RC misc 0x10161000; no UART transmit; no blind flash;
* **never run the boot ladder / hcc_arm / hcc_loop / dp_setup / release on
  endpoint 0001:00:00.0** (second host view of the same chip);
* exception mask / page change / mask != 0 -> power-cycle, capture evidence,
  no retry in place.

---

## G0 - preconditions (read-only)

    uname -r                                   # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko
        # expect 784608410b4dd8e4c74be9b923d62465d4c397d12e84ee2ce58710efcc33de02 (359,992 B)
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep -c '^parm:'   # 58
    modinfo -p /lib/modules/HI_RELEASE/hi5622_wifi.ko | grep '^ba_enable:' # (uint)
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin
    ls /lib/firmware/regulatory.db

Golden-state gate (read-only):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                      # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/param count is not the lane artifact: wrong build.

## B0 - cold boot + link + console (read-only)

    iw reg set US && iw reg get                # country US / 5 GHz IR
    tools/rsh.sh 'dmesg -c >/dev/null'          # clear the ring

PASS: no exception line in the UART log, page golden, bench link PASS.
STOP: age/exception/interrupt-storm -> power-cycle, capture, report.

## B1 - load with `ba_enable=0` (regression rung; this is the default)

Load the **same parameter set as the last proven 2.4 GHz AP rung** plus
`ba_enable=0` (the base module's proven config; the skeleton adds no other
behaviour). Example of the proven-shaped line:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko   # if not already loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 band5g=0 ba_enable=0

    cat /sys/module/hi5622_wifi/parameters/ba_enable    # 0

PASS (all): rc=0; the same boot/AP lines as the proven base module;
hostapd AP-ENABLED (2.4 GHz fixture); page 0x86807FEF / state 1 / mask 0;
**dmesg contains no `ba:` line** (the core is off). This is the "no
regression" proof: the BA code is compiled in and *inert*.

STOP: any page/mask change, a `ba:` line, or a param-count/sha mismatch.

## B2 - load with `ba_enable=1`, no peer (the fail-closed smoke)

Fresh boot. Same line as B1 but `ba_enable=1`:

    cat /sys/module/hi5622_wifi/parameters/ba_enable    # 1

Start the same 2.4 GHz open AP and hostapd as the proven rung. Then capture
**60 s of the AP channel** with the external observer (the
`../wifi-onair-observer` harness: passive monitor capture on the AP's
channel) and grep for 802.11 management **action** frames with category 3
(BA). Also watch dmesg.

PASS (all):
1. hostapd AP-ENABLED, beacons on air as in the proven rung;
2. page 0x86807FEF / state 1 / mask 0;
3. **zero** 802.11 action/category-3 frames from the AP's BSSID in the 60 s
   capture (no peer is associated, so no BA may be negotiated; the skeleton
   cannot emit one anyway - this asserts it);
4. **zero** `ba:` dmesg lines (nothing calls the core until integration);
5. no firmware exception, no hcc timeout.

FAIL: any BA frame, any `ba:` line, page/mask change, exception ->
STOP, capture (dmesg + observer pcap + UART), power-cycle, report. Do not
retry in place.

## B3 - BLOCKED ON RX: the BA session setup (integrated module only)

**Preconditions:** the RX gate is passed (a real client associates - the
RX-1..RX-5 ladder) AND the skeleton hooks are wired (REPORT.md sec. 9). Fresh
boot, module with `ba_enable=1`.

1. Start the AP, associate the observer client (WPA2 open or PSK fixture).
2. Drive traffic toward the client; the driver either gets a fw D2H BA create
   (id 2 / 0x1D action 0) or initiates the TX BA after the first QoS data.
3. Peers respond.

PASS (all):
1. sniffer/observer shows an **ADDBA request** from the AP's BSSID to the
   client (33-byte action frame, category 3 action 0);
2. the client's **ADDBA response** is received (driver dmesg
   `ba: relay addba rsp (active)`) and the driver posts the g0 0x0A relay
   (dmesg frame log shows the 44-byte id-0x0A frame consumed by the firmware);
3. dmesg TX session state active (or debugfs status if wired) for the TID;
4. page 0x86807FEF / state 1 / mask 0; no firmware exception.

FAIL: no ADDBA on air, relay not consumed (`announce NOT consumed`), status
nonzero, page change -> STOP, capture the exact frame, power-cycle, report.
Record which step failed and what it falsifies (negotiation vs fw relay vs
state machine).

## B4 - BLOCKED ON RX/B3: the A-MPDU aggregation window (integrated only)

With B3 active, send a burst (e.g. `iperf3 -c <client> -t 10`) from the AP.

PASS (all):
1. dmesg `ba: ampdu start` then `ba: ampdu end` for the TID, each backed by
   a consumed g0 0x0B frame (20 B total, id 0x0B, start payload+0 = 0, end = 1);
2. the on-air capture shows **A-MPDU aggregated PPDUs** to the client (more
   than one MPDU per PPDU) or a measurable aggregation in the client's
   statistics;
3. no firmware exception, page golden.

FAIL: no g0 0x0B frames consumed, no aggregation on air, or exception ->
STOP, capture, power-cycle. This is the rung that decides whether the
decoded aggregation-window messages are the right minimal implementation.

## B5 - BLOCKED ON RX/B3: teardown (integrated only)

Stop traffic / disconnect the client / send a DELBA trigger.

PASS: DELBA action frame on air (30 B, category 3 action 2), driver posts the
g0 0x0A subtype-2 relay (payload+1 = 2), TX session cleared, page golden.
FAIL: no DELBA / relay / stale session -> STOP, capture, power-cycle.

## B6 - BLOCKED ON RX/B3: throughput smoke (integrated only)

`iperf3` both directions for 10 s. PASS: non-zero steady throughput with
aggregation and no exception; record the number as the BA-1 baseline (the
pre-BA baseline is in `../wifi-power-perf`). FAIL: classify (no BA / no
aggregation / exception) and stop.

## B7 - the fail-closed negative (integrated or skeleton, host-side observable)

1. Load with `ba_enable=2`: the first BA entry point must return -EINVAL and
   emit **nothing** (no frame, no H2D message); dmesg may log a refusal.
2. Load with `ba_enable=0` and let a peer send an ADDBA request: **no
   response, no relay, no crash** (fail closed). The peer retries; the AP
   ignores.
PASS: no BA frame/message in either case, page golden, no exception.
FAIL: any traffic when off/invalid -> STOP, capture, report (a fail-closed
breach is a blocker).

## If B1/B2 PASS (what today's fire buys)

* the skeleton is compiled, linked and provably inert by default;
* arming `ba_enable=1` does not change the AP's on-air behaviour with no
  peer (no spurious BA frames);
* the decoded message/state machinery is ready to be wired as soon as the RX
  gate allows a real peer.

Record: sha256, param count, the B1/B2 PASS lines, page line, observer
capture path. The next rung is the **RX gate**, then the integration hooks
(REPORT.md sec. 9), then B3-B6 on a fresh boot - never a hostapd restart on
the same load.
