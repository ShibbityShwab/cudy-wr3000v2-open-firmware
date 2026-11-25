# FIRE-RECIPE - the field-B fix (F2): 0x01F7 must address the band-1 context

**Lane:** wifi-fw-dfs-worker (task-191). This lane never fires. The Lead fires
with the resident bench procedure (bench link PASS, recovery card READY, human
at bench, UART capture read-only). One change per fire, one rung per fresh
boot, no in-place retry.

**The question F2 answers:** with the 0x01F7 frame's field B corrected to 0
(the AP-start module index), does the worker run on the **band-1** object -
mode byte 0x15F474 = 3, the ch36..64 RF image written - and does CA 0xA2FA2
then programme the beacon?

Inherit every non-negotiable from ../wifi-5ghz-beacon/FIRE-RECIPE.md and
../wifi-dfs-mode-send/FIRE-RECIPE.md: recovery armed; UART read-only; never
write CA 0x400392f0; never read the RC misc window 0x10161000..0x1016ffff;
never rmmod the vendor modules; page/state/mask anomaly -> power-cycle; one
rung per fresh boot.

## P0 - read-only discriminator on the STILL-LIVE F1 AP (no fire)

The F1 AP is up with the 0x01F7 frame already fired. Read the pack in
TABLES T5 (#1-#15). Verdicts:

| observation | meaning |
| --- | --- |
| u32(0x11C910) = 0x0015CA08 and u8(0x15D90C) = 3 | **H2 confirmed**: the fired worker ran and programmed band 0. The 5 GHz object was never the target. Go to F2a. |
| u32(0x11C910) = 0 and u8(0x15D90C) = 0 | H1: module 4 was unbound; the worker exited at log 1442. Same fix. |
| u32(0x11C910) != 0x0015CA08 and != 0 | unexpected binding; capture and report, do not fire |
| u16(0x171504) != 0 | add it to every 0x11xxxx probe address |

Also record band 1 baseline: u32(0x119C10)=0x0015E570, u32(0x15FF70)=0x00175A18,
u8(0x15F2E0)=1, u8(0x15F474)=0, u32(0x175CF0)=0x400B2000.

## F2a - rebuild with field B = vap_start_module (0), then fire

Source change (two one-byte edits; patch attached as
patches/0001-fieldb-context.patch; the implementation lane must rebuild and
update the lane-190 golden-frame host tests):

1. `hi5622_dfs_mode.h`: `HI5622_DFS_MODE_FB 0x04u` -> `0x00u`
   (or better: pass `hi5622_vap_start_module` as the fb argument).
2. `hi5622_desc_table.c`: `b[3] = 0x04u` -> `0x00u` (same rule).

Fixed 0x01F7 frame: `000100001d00010000005a5a0000000001000000f701010003ffffffff`
(29 B; only byte [3] changed 0x04 -> 0x00). The 0x00F7 frame becomes
`00010000...` with the same hdr[3]=0.

Then the SAME load line as the F1 fire
(`vap_start_module=0 ... beacon_static=1 beacon_vap_start=1 band5g=1 vap5g=1
ap_chan_wait_ms=300 desc_table=1 dfs_mode=1`), the same boot ladder + C2 pair,
the same ch40 (5200 MHz) AP start, and the read-only verification:

| # | read | expected |
| ---: | --- | --- |
| 1 | dmesg plan line | `vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start` |
| 2 | dmesg `dfs_mode post` line | 29 B, key 0x01F7, body mode=3, after desc_table, before ap_chan |
| 3 | u8 band1+0xF04 (0x15F474) | **3** (the gate CA 0xA2FA2 requires) |
| 4 | u32 band1 R (0x175CF0) | 0x400B2000 (unchanged pointer) |
| 5 | u32 R+0x3A8 | **0x01800060** (ch36..64 image) |
| 6 | u32 R+0x3AC | **0x03E6138A** |
| 7 | u32 R+0x410 bit0 | 1 (predicate latch) |
| 8 | observer scan on 5200 MHz | the AP's beacon |
| 9 | u8 band0+0xF04 (0x15D90C) | stays 0 - the frame must no longer touch band 0 |

## F2b - only if F2a leaves mode 0

Do not blind-loop. Capture first: (a) the dmesg post/plan lines; (b) the
probe pack (T5); (c) the frame hex from the dmesg/status line; (d) the
firmware exception mask/page state. Then decide between: the frame was not
posted (host plan), the frame posted with the wrong byte (build drift), the
band-1 context was rebound after vap_add (probe #5), or a second gate in
CA 0xA2FA2 (the sibling rf-programme lane's decision tree).

## Abort

Any page/exception/mask anomaly, any unexpected probe value at 0x11C910 once
H2 is disputed, any dmesg abort: power-cycle, no retry, report the dump. Never
write firmware RAM by hand; the probe pack is read-only.
