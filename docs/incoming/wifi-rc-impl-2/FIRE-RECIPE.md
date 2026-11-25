# FIRE-RECIPE - RC-2: the rate_ctl SET rate_mode auto retry with the corrected C2

**Lane:** rc-impl (task-200). **The Lead fires**; this lane made no bench
contact. **One idea per boot. One load per boot. Never retry in place.** If a
gate below does not hold: STOP, capture the read-only pack, power-cycle.

Inherited non-negotiables (BENCH-FACTS / LESSONS / RUNBOOK): recovery door
armed (RESET held through the counter + TFTP ready) before any fire; UART
capture READ-ONLY to an absolute path; bench link PASS; no human at the bench =
no fire; never rmmod after the CPU release; never write CA 0x400392f0; never
read RC misc 0x10161000; no UART transmit; no blind flash; a stall/exception
means capture + power-cycle, never a second message on that boot.

## Module under test

The RC-2 activity module (built by `tools/build-rc2.sh`, identities in
`evidence/build-module.txt`):

    /tmp/rc2-artifact/hi5622_wifi.activity.ko
    size 401,884 B  sha256 3d43a3cd261d28245e47a964161a0d605caae97a70baa81bea1eee630e294536
    .text sha256 ba81e4b38b4a965143b7a7689c9639525e9af9852e05c968c9da65ff42ab7839  (stable identity)
    67 params, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends=cfg80211

Copy it over the same deploy slot the h2d-send/5GHz ladders use
(`/lib/modules/HI_RELEASE/hi5622_wifi.ko`).  The module is the IMAGE #3
merged base (64 params) + the RC-2 surface; its new attributes are
`h2d_ctx_attest` (RO) and the `rc_*` group (RO `rc_status`/`rc_log`).
Never rmmod after the CPU release: a reload is a fresh golden boot.

## The one thing that changed vs the 2026-11-27 RC fire

The old recipe wrote `echo 3 > h2d_ctx_create`.  **Do not.**  The corrected C2
requires **type 1** and its read-only attestation **before** `h2d_txq_sync72`:

    echo 1 > $D/h2d_ctx_create          # TYPE 1 (not 3)
    cat $D/h2d_ctx_attest               # require: valid=1 ctx_create_type=1,
                                        #          ctx190 != 0, ctx434 != 0
    echo 1 > $D/h2d_txq_sync72          # only after the attestation passed

If `h2d_ctx_attest` reports `valid=0`, STOP (capture; power-cycle; no TXQ
frame, no RC frame).  The driver itself refuses the TXQ frame in that state
(fail-closed) - the command lets the Lead see why.

## R-1 (negative control, no message) - default-off must refuse

Load with the default `rate_ctl=0` (and the normal AP ladder).  After the core
phase is ACTIVE:

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/rc_status                    # enabled=0, precond ctx_type1=0/...
    echo auto > $D/rc_rate_mode         # expect EPERM
    dmesg | tail -3                     # "RATE_CTL: op 0 cfg 401 refused (disabled)"
    cat $D/rc_status                    # committed=0/... refused=1

PASS: the write fails closed, no frame, no dmesg H2D/frame-post line.

## R0 - preconditions (read-only), one boot

    uname -r                                           # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko   # the lane artifact (activity sha)
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko -F parm | grep -c '^rate_ctl'   # 3
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko -F vermagic                     # 6.18.60 ...
    sha256sum /lib/firmware/hi_wifi/FIRMWARE.PD4.bin   # cfe24bfb...037b1
    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                              # page 0x86807FEF / state 1 / mask 0

STOP if the module identity/params, the firmware sha or the golden gate differ.
Also confirm the RO attribute exists: `ls $D/h2d_ctx_attest $D/rc_status`.

## R1 - the corrected ladder + the 2.4 GHz AP + the SET (the retry)

Load ONE time, then run the ordered ladder:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko            # if not loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin \
        wifi_block_path=hi_wifi/wifi_block.bin \
        h2d_send_budget=8 rate_ctl=1 rate_ctl_fb=4 rate_ctl_arbitrary=0

Expected dmesg: the usual two-band ready-handshake lines plus
`RATE_CTL: enabled (fb=4 arbitrary=0)`.  STOP if the params read back wrong.

    D=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $D/prepare ; echo 1 > $D/upload ; echo 1 > $D/verify
    echo 1 > $D/seed ; echo 1 > $D/hcc_arm ; echo 1 > $D/hcc_loop
    echo 1 > $D/release                                   # 0x5A5A ONLY

    # --- the CORRECTED C2 (do not vary; type 1, never 3) ---
    echo 1 > $D/h2d_ctx_create
    cat $D/h2d_ctx_attest        # PASS = valid=1 ctx_create_type=1
                                 #        ctx190 != 0 && ctx434 != 0
                                 # STOP if valid=0 (capture + power-cycle)
    echo 1 > $D/h2d_txq_sync72
    cat $D/h2d_status            # phase=ACTIVE ok=2 timeout=0; the
                                 # "corrected-C2: ctx_create_type=1 ctx190_attest=1 ..." line
    cat $D/h2d_ctx_attest        # re-read: valid=1 (the ordered frame was accepted)

    # --- the 2.4 GHz AP (the steady ALG state; the bench's proven open AP) ---
    # start the standard 2.4 GHz open AP ladder used by the BA/h2d-send fires
    # (hostapd wlan0 open, channel 6, hw_mode=g).  The AP-ENABLED line and the
    # page still golden:1 are the gate.
    cat $D/boot $D/status        # page 0x86807FEF / state 1 / mask 0
    cat $D/rc_status             # precond ctx_type1=1 ctx190_attest=1 txq72=1

    # --- the ONE rate-control message: SET rate_mode auto ---
    echo auto > $D/rc_rate_mode
    # expect (dmesg):
    #   RATE_CTL: corrected-C2 attestation at send: valid=1 ...
    #   RATE_CTL: sent cfg 401 type SET (op 0) alg_cfg blob 11 B on the
    #             g0/id2 key-0x0101 config event (fb=4) as the 39 B frame;
    #             out[0]=0x00000000 consumed
    #   RATE_CTL rsp: g1 id3 syn 0x0101 cfg 401 type SET dev_ret 0 value_len 1
    cat $D/rc_status
    cat $D/rc_log

R1 PASS (all of):
1. `h2d_ctx_attest` valid=1 with ctx_create_type=1 and ctx190/ctx434 nonzero
   BEFORE the txq_sync72 write;
2. `h2d_status` phase=ACTIVE ok=2 timeout=0 after the TXQ sync;
3. the 2.4 GHz AP reaches AP-ENABLED with page golden:1;
4. `echo auto > rc_rate_mode` returns 0; `rc_status` shows
   `last_decision=allow last_cfg=401 last_type=SET`,
   `last_blob_len=11 last_blob=91 01 00 0d 00 00 00 00 01 00 00`,
   `last_wire_len=39 last_wire=00 01 00 04 27 00 02 00 00 00 5a 5a ...`,
   `out0_after=0x00000000`;
5. exactly ONE `RATE_CTL rsp` line with syn 0x0101, cfg 401, type SET,
   dev_ret 0, body_len 11, value_len 1 (rc_log shows the request blob with
   dev_ret updated);
6. page still 0x86807FEF, state 1, mask 0.

STOP (capture + power-cycle, no second message on that boot): no attestation
after type-1 create; TXQ sync refused/stalled; the AP fails; the RC frame's
announce is not consumed (`out[0]=0x8` class) or the firmware faults; a
malformed rsp (wrong cfg/syn/body length); mask != 0; page lost.

## R2 (same load, one more message if the budget permits) - GET rate_mode

    echo 1 > $D/rc_rate_mode_get
    cat $D/rc_status
    cat $D/rc_log

PASS: one rsp for cfg 401 type GET dev_ret 0, value_len 1, value byte (blob+10)
= **00 = auto**.  `value=01` would mean a fixed mode is in force (record; do
not retry).  This is the firmware-reported rate-selection observable.

## R3 (optional, same load if budget permits) - the fixed contrast

    echo fixed > $D/rc_rate_mode          # cfg 401 value 01
    echo 1 > $D/rc_rate_mode_get          # rsp value must now be 01
    echo auto  > $D/rc_rate_mode          # restore adaptation
    echo 1 > $D/rc_rate_mode_get          # rsp value 00 again

Budget note: `allow_frames` (default 16) bounds the RC frames; the h2d ladder
uses `h2d_send_budget=8`.  R1+R2+R3 = 5 RC frames, well inside the budget.

## Read-only capture pack (collect on every arm, before any power-cycle)

`dmesg` (grep H2D|RATE_CTL|frame post), `cat $D/rc_status`, `cat $D/rc_log`,
`cat $D/h2d_status`, `cat $D/h2d_ctx_attest`, `cat $D/boot $D/status`, the
UART log absolute path, and the AP client's scan result (the rate-change
observable, optional).

## STOP RULES (any -> power-cycle, capture, report)

* the type-1 attestation does not pass, or the TXQ sync is refused/stalled;
* the announce does not clear (out[0]=0x8 class) or a timeout/exception line;
* mask != 0 / page lost / exception staging nonzero;
* a second rate message class on the same boot, or more than 5 RC frames;
* any rsp whose cfg id is not the sent id, or a body that is not the request
  blob, or an out-of-enum GET value;
* any `h2d_desync` increment.

## What NOT to do

* Do not write `h2d_ctx_create=3` (the old recipe); it is the documented
  fault class and the driver now refuses it.
* Do not re-send a stalled frame; do not bypass the attestation by writing the
  TXQ sync first (the gate refuses).
* Do not change `rate_ctl_fb` (4 = the module the type-1 create made).
* Do not write CA 0x400392f0, never read 0x10161000, no UART transmit.
