# FIRE-RECIPE - the vendor alg_cfg rate-control ladder (wifi-rate-ctl)

**Lane:** wifi-rate-ctl (task-179). **The Lead fires**; this lane never
contacted the router.  **One-shot per load.  One idea per boot.**  If a gate
below does not hold: STOP, record, power-cycle; never retry in place.

Module under test: the host-built patched module from BUILD-NOTES.md -
**471,580 B, sha256 96c541599a73a8f2ae6fa020cdfc6ce5767cbf33aa19d637a92ebd94cbd42f78** (see BUILD-NOTES for the exact artifact
of the build you deploy; the sha is build-path dependent because of debug
info, so compare `.text` or the printed sha of your build).
It is the wifi-h2d-send lane module + `patches/0001-rate-ctl.patch`:
same transport/HCC/config-event path, same gates, plus the default-off
`rate_ctl` surface.

Inherited non-negotiables (BENCH-FACTS / LESSONS / RUNBOOK):

* recovery door armed (RESET held through the counter + TFTP ready), UART
  capture running READ-ONLY to an absolute path, bench link PASS; no human at
  the bench = no fire;
* never rmmod after the CPU release; never write CA 0x400392f0; never read RC
  misc 0x10161000; no UART transmit; no blind flash; never run the boot ladder
  on endpoint 0001 (second host view of the same chip);
* exception mask / page change / mask != 0 -> power-cycle, capture, no retry;
* never 0xCECE during boot (release 0x5A5A only);
* one H2D idea per boot; the budget `h2d_send_budget` is shared by every
  post-ready frame (this module allows 8, a supervised session only).

Why this module: the rate-control commands are **device-processed alg cfg
config events** (need_d_process + need_w4_d), carried exactly like the
calibration ladder that the bench already proved: HCC group 0 / id 2 /
key 0x0101, payload = cookie0|cookie1|key|len|alg_cfg blob.  The patched
surface only builds the decoded blobs; every frame goes through the existing
SR-node commit, coalesced mailbox id-3 announce and bounded out[0] poll.

---

## R-1 (optional negative control) - default-off must refuse

Load with the default `rate_ctl=0` (no extra param). After the boot
ladder reaches ACTIVE (so the gates would otherwise allow a frame):

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/rc_status                      # enabled=0 rate_ctl=0
    echo auto > $D/rc_rate_mode           # expect: EPERM
    dmesg | tail -3                       # "RATE_CTL: op 0 cfg 401 refused (disabled)"

PASS: the write returns an error, the dmesg refusal line appears, and
`h2d_frames_sent` is unchanged (no frame).  This is the fail-closed proof;
it costs no message.

## R0 - preconditions (read-only), one boot

    uname -r                                          # 6.18.60
    sha256sum /lib/modules/HI_RELEASE/hi5622_wifi.ko  # your lane artifact
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko -F parm | grep -c '^rate_ctl'   # 3
    modinfo /lib/modules/HI_RELEASE/hi5622_wifi.ko -F vermagic                      # 6.18.60 ...
    ls /lib/firmware/hi_wifi/FIRMWARE.PD4.bin

    D=/sys/bus/pci/devices/0000:00:00.0
    cat $D/boot $D/status                # page 0x86807FEF / state 1 / mask 0

STOP if the module sha/params are not the lane artifact or the golden gate is
not exactly page 0x86807FEF / state 1 / mask 0.

## R1 - load + boot ladder + the first rate message (SET rate_mode auto)

This rung needs the ordered C1/C2 precondition ladder (the alg config event is
posted only after the core phase is READY_TXQ_SYNC/ACTIVE and the video path
is armed).  Load ONE time:

    insmod /lib/modules/HI_RELEASE/cfg80211.ko            # if not loaded
    insmod /lib/modules/HI_RELEASE/hi5622_wifi.ko         fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin         h2d_send_budget=8 rate_ctl=1 rate_ctl_fb=4 rate_ctl_arbitrary=0

Expected dmesg: the same two-band ready-handshake lines as the h2d-send ladder
plus `RATE_CTL: enabled (fb=4 arbitrary=0)`.
STOP if `rate_ctl`, `rate_ctl_fb` or `rate_ctl_arbitrary` read back
wrong (wrong module/params) or the module sha differs.

    D=/sys/bus/pci/devices/0000:00:00.0
    echo 1 > $D/prepare
    echo 1 > $D/upload
    echo 1 > $D/verify
    echo 1 > $D/seed
    echo 1 > $D/hcc_arm
    echo 1 > $D/hcc_loop
    echo 1 > $D/release          # 0x5A5A ONLY
    echo 3 > $D/h2d_ctx_create   # ordered precondition (type 1 = the bench default)
    echo 1 > $D/h2d_txq_sync72

    cat $D/h2d_status            # phase=ACTIVE ok=2 timeout=0
    cat $D/rc_status             # budget=8 sent=0/8 refused=0 rsp=0

Then the R1 message - the deterministic "rate adaptation AUTO" config:

    echo auto > $D/rc_rate_mode
    # expect: write returns 0 and dmesg:
    #   RATE_CTL: sent cfg 401 type SET (op 0) alg_cfg blob 11 B on the g0/id2 key-0x0101 config event (fb=4)
    #   H2D rate_cfg: frame 39 B in a 39 B node ... CONSUMED after N polls
    #   RATE_CTL rsp: g1 id3 syn 0x0101 cfg 401 type SET dev_ret 0 value_len 1

    cat $D/rc_status
    cat $D/rc_log

R1 PASS (all of):
1. the write returns 0; `rc_status` shows last_decision=allow, last_cfg=401,
   last_type=SET, last_blob=91 01 00 0d 00 00 00 00 01 00 00;
2. the H2D frame is CONSUMED (out[0]=0 within the bounded window; dmesg
   "CONSUMED"; `h2d_status` timeout=0);
3. exactly ONE `RATE_CTL rsp` line with syn 0x0101, cfg 401, dev_ret 0
   (`rc_status` last_rsp block: body_len=11 value_len=1 value=00); the
   response body in `rc_log` is the request blob with dev_ret updated;
4. page still 0x86807FEF, state 1, mask 0.

STOP: no rsp within the bounded window, a malformed rsp (wrong cfg id / body
len / syn), out[0] not consumed, mask != 0, any exception -> power-cycle and
record.  Do not send a second message on that boot.

## R2 - the rate-mode readback (GET rate_mode), fresh boot per load? NO:
## same load is allowed (different idea, budget permits), one message only:

    echo 1 > $D/rc_rate_mode_get
    # expect: RATE_CTL: sent cfg 401 type GET ... ; H2D rate_cfg 39 B CONSUMED;
    #         RATE_CTL rsp: g1 id3 syn 0x0101 cfg 401 type GET dev_ret 0 value_len 1
    cat $D/rc_status

R2 PASS: the rsp's `value` byte at blob+10 is **00 = auto** (the GET answers
the firmware's actual rate mode; `rc_status` prints it).  This is the
rate-selection observable: mode auto (adaptation active) vs fixed.
`value=01` would mean a fixed mode is in force - record it (the SET in R1
should have made it 0; a mismatch is a finding, not a retry).

## R3 (optional, one more message if budget allows) - the fixed-mode contrast

Same load, third idea (R1/R2/R3 total 3 frames <= budget 8):

    echo fixed > $D/rc_rate_mode
    echo 1 > $D/rc_rate_mode_get      # readback must now answer 01 = fixed
    # to restore adaptation: echo auto > $D/rc_rate_mode
    cat $D/rc_status

PASS: the GET answer flips to 01 after the fixed SET (and back to 00 after the
restore) - a direct firmware-reported rate-selection observation.
The "vs the default/static rate" comparison is exactly this pair.

Alternative R3-TPC (per-rate power control, RC-2 input): `echo "ctrl 3" > $D/rc_tpc_pow`
(set tpc_pow_lvl mode=ctrl level=3); expect a rsp for cfg 1802.  Does not
change the rate mode.

## R4 (optional, later session) - the selected-rate report

`echo 02:00:00:00:00:01 > $D/rc_ration_get` (cfg id 421, GET
get_current_ration; a real associated user MAC when one exists).  Expected:
one rsp for cfg 421, body_len 30; `rc_log` records the raw value area.
A dev_ret error (user not exist) is a valid answer - record it as such.

## STOP RULES (any -> power-cycle, capture UART+dmesg+rc_status+rc_log, report)

* exception / mask != 0 / page change / "announce NOT consumed" / lane closed;
* a second NEW message class on the same boot, or more than 3 messages;
* any rsp whose body cfg id does not equal the sent cfg id, or a body that is
  not the request blob (the sync contract);
* a GET readback value outside the decoded enum for that command;
* H2D timeout or any `h2d_desync` increment.

## What to capture for the report

`dmesg` (the RATE_CTL + H2D + D2H lines), `cat $D/rc_status`,
`cat $D/rc_log`, `cat $D/h2d_status`, `cat $D/boot $D/status`,
the UART log path.  The response body hex in `rc_log` is the evidence for
the decoded contract.

## Notes for the bench tooling lane

The new sysfs attributes are `rc_rate_mode`, `rc_rate_mode_get`,
`rc_ar_set`, `rc_ar_get`, `rc_tpc_pow`, `rc_tpc_get`,
`rc_ration_get` (WO), `rc_send` (WO, needs rate_ctl_arbitrary=1),
`rc_status` / `rc_log` (RO).  Add them to the managed
bench_guards allowlists before a managed sequence, or run the rungs above as
direct echo commands under the attended procedure.