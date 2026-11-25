# FIRE-RECIPE - the 802.11ax advanced machinery (task-195)

**The Lead fires.**  This lane is host-only: nothing here was run on the
device.  R0 and R1 are **read-only / zero-rebuild**; R2 is the only rung that
posts frames and it needs a module built with the `src/` AX-event model
(the implementation lane's build, not this lane's).  Never write CA
0x400392F0, never read the RC misc window 0x10160000..0x1016FFFF, never
`rmmod` the vendor modules.  One boot per ladder; do not re-send a frame
that was already consumed in the same boot.  The recovery door must be READY
and the page must be golden before any fire.

Conventions: firmware RAM phys = `0x40000000 + CA + 0x6B8000`; device IO
phys = `0x40000000 + 0x3B8000 + (CA - 0x40000000)`; read with the committed
read-only helper (O_RDONLY on the PCIe resource0, phys addressed).  Keep
every read; they are the evidence.

The whole AX decode is in [TABLES.md](TABLES.md); the two frames this rung
posts are in [T12](TABLES.md#t12-our-own-host-built-frames-protocol-bytes-not-vendor-bytes-v)
and in `evidence/frames.txt`.  Field B (the per-module context selector)
must be the AP-start module index - **0** in the fired 2.4 GHz ladder
(../wifi-fw-dfs-worker).

## R0 - read-only probe pass (no writes, one boot, ~26 reads + one scan)

Goal: confirm the decoded field map on the live firmware and resolve the
three runtime ALG handler addresses.  Run on the live 2.4 GHz HE AP before
any rebuild.

1. **Context**: `u32 0x171458` (expect 0x00119758), `u16 0x171504` (the
   base offset to add to every pointer; expect 0).  Then `u32 0x1198E8`
   = `ctx0+0x190` = the AP's VAP pointer (expect nonzero; chase it).
2. **TWT/SR module bytes**: `u8 0x119832` (ctx0+0xDA; expect 0 before any
   enable), `u8 0x119846` (ctx0+0xEE; expect 0).
3. **ALG handler slots** (the decisive part):
   `u32 0x174B0C`, `u32 0x174B10`, `u32 0x174B14`.  Expect nonzero
   Thumb code addresses; record them - they close the last static decode
   gap (the `sch`/`cmn`/`err_name` bodies).
4. **Hook cross-check**: `u32 0x174B34` (CBH+0x370, expect 0 in this
   image) and `u32 0x174B38` (CBH+0x374, expect 0x00057639).
5. **Band sanity** (same values as the 5 GHz recipe): `u32 0x15E408`
   (band0+0x1A00, expect 0x00174BF0), `u8 0x15D90C` (band0+0xF04),
   `u32 0x15FF70` (band1+0x1A00, expect 0x00175A18), `u8 0x15F474`
   (band1+0xF04).  On a 2.4 GHz-only boot band 0 is the live band; a band
   that was never started reads zero - record, do not interpret as a fault.
6. **VAP-relative** (second step; `VAP = u32(0x1198E8) + u16(0x171504)`):
   `VAP+0x1A4`, `+0x1A6`, `+0x1A7`, `+0x1A8`, `+0x1D4..+0x1E4` (read
   `+0x1D4` for AC0 at least), `+0x364..+0x367`, `+0x3DE`, `+0x831`.
7. **On-air**: scan the AP from the observer and record the HE Capabilities
   and HE Operation elements (EID 0xFF ext-IDs 0x23/0x24): capabilities
   bytes, HE Operation Parameters (expect `0x003FF4`), BSS Color
   Information byte.  The 2026-11-27 bench reference was Color 25.

**PASS**: context pointer chain resolves, the ALG handler slots are nonzero,
the VAP fields are 0 (no AX enable posted yet), and the HE IEs match the
recorded reference.  **FAIL**: a zero VAP pointer (chase the context table
again after the next boot), or no HE IEs at all (do not proceed to R2 - the
AP is not the HE AP).  No writes, no abort risk.

## R1 - zero-rebuild BSS-color / HE-IE differential (no module, no writes)

Goal: classify where the on-air BSS color comes from without touching the
kernel.  Three AP restarts, same boot, no module rebuild.

1. Current config: record the HE Operation params + BSS Color (R0 step 7).
2. Set a distinct hostapd `he_bss_color=<n>` (any value 1..63) in the AP
   config and restart hostapd/AP; rescan; record the color.  If hostapd
   refuses the option, record that (it is itself evidence: the shipped
   hostapd gates he_bss_color on HE support).
3. Change the SSID (and, if easy, the channel) and restart; rescan; record.
4. Optionally power-cycle the chip (fresh boot) and rescan - a color that
   changes across boots would be derived from boot state rather than a
   constant.

**PASS (all four reads identical)**: the BSS color is a firmware constant;
AX-6 "change the color" is not a host-patchable feature - it needs the
firmware-side path and is out of scope for a host-only lane.  **FAIL**
(color follows a host setting)**: a host->firmware color channel exists that
this decode has not identified - re-open it (the ALG SR param 0x0CE5 path is
the prime candidate).

## R2 - the AX config-event post rung (implementation fire; one boot)

Prerequisite: a bench module built by the implementation lane from
`../src/hi5622_ax_events.{c,h}` with a default-off parameter (e.g.
`ax_events=0` / a sysfs list), posting the events **after** AP start on
field B = the AP-start module index (0).  This lane does not build or fire it.

Order (one frame per step, read-back after each; all 29/32-B frames from
TABLES T12):

1. Boot the HE AP (the proven 2.4 GHz ladder).  Always validate the page
   golden and the AP-ENABLED state first.
2. Post the **TWT enable** frame body `01`:
   `000100001d00010000005a5a00000000010000005d03010001ffffffff`.
   Read `ctx0+0xDA`; PASS = bit6 set (0x40).  Then the optional second step
   body `03` sets bit7 too (0x80|0x40).
3. Post the **ext TWT IE on** frame body `01000000`:
   `000100002000010000005a5a0000000001000000e900040001000000ffffffff`.
   Read `VAP+0x3DE`; PASS = 1.
4. Post **MU EDCA selector 0x5B, AC0** with value `0x00F0F0` (body
   `5b00000000000000f0f00000`):
   `000100002800010000005a5a00000000010000005b000c005b00000000000000f0f00000ffffffff`.
   Read `VAP+0x1D4`; PASS = 0x00F0F0.
5. Post **BFEE on** (`01`, key 0x0335): read `VAP+0x364`; PASS = 1.
6. Post **multi-AP SR on** (`01`, key 0x04B6): read `ctx0+0xEE`; PASS = 1.
7. **Optional ALG path probe (white-box safe)**: post an ALG text frame whose
   token[0] is a module name that does not exist (e.g. `zz`); the worker
   logs "no handler" and returns - it cannot touch the scheduler.  This
   validates the key-0x0128 carrier without changing algorithm state.  Do
   NOT post a valid `sch ...` frame until the R0 handler decode is done.
8. Final: scan the AP again and diff against R0 step 7 (any HE-IE change is
   a bonus observation, not a gate); page golden; capture dmesg.

**PASS**: every read-back field changed as decoded and the page stayed
golden.  That proves the clean-room driver can drive the vendor AX feature
switches - the host half of AX-5 (TWT enable) and the AP-side BF/SR enables.
**FAIL**: first read-back unchanged -> worker not reached (key, carrier,
field B); the decoded body/offset is wrong only if the page/transport is
healthier than the field - stop and capture before any retry.  A non-golden
page = stop, power-cycle, no retry (the frame was harmful).

## R3 - blocked (needs the RX gate / an associated AX client)

Not testable on the current AP, stated so nobody claims them:

* DL/UL OFDMA: needs an associated HE client and an unblocked RX path; the
  observable is an HE trigger frame / RU allocation and client data in the
  assigned RU.  The host has nothing to post - the vendor scheduler
  generates it.
* MU-MIMO: needs 2 clients and the sounding NDPA/NDP exchange; the host
  side is per-user state in the g0 id8 message (AX-2) plus the ALG sounding
  params.
* TWT agreements: needs a client; only the per-user D2H sync
  (`hdpp_d2h_user_twt_syn`) exists as a host handler today.
* MU EDCA/TWT **on-air elements**: whether the firmware mode-10 beacon
  builder emits them is [U]; R2 step 8 is the first empirical answer.

## Abort rules

* Non-golden page, exception mask nonzero, or a hung H2D consumer: stop,
  capture UART/dmesg, power-cycle; no retry.
* Any probe read that returns all-0xFF or bus-fault: stop (the BAR mapping
  assumption is wrong; re-run the read-only inventory validator).
* Never write; R2 only posts the listed frames and only after AP start.
