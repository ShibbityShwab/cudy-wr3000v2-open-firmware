# HCC post-transport-boot contract — SUMMARY

**Lane:** hcc-re (task-63), host-only, 2026-11-22. Full detail: [REPORT.md](REPORT.md).

## One-line verdict

The contract for everything after the working transport (B1+B2) is now pinned: the device's
README-ready sequence is **mailbox bitmap `0x4`** (platform ETE up, no host reply, must be drained)
then **HCC chip-group-4 messages id 1 and id 2** delivered over an ETE DR ring (they release the
vendor's 200/2000-jiffy init waits); host→device traffic is one 12-byte HCC header in an SR node
announced with **mailbox id 3**; D2H is the 11-id mailbox dispatch {1,3,6,7} feeding the HCC
group/id handler tables (all five catalogues decoded with handler names).

## The 10 contract points

1. `out[1]=0x4` = platform-up announce; vendor mailbox layer has **no id-2 handler**, drop after
   ack/drain/rearm. Real ready gate = HCC g4 id1 (`device_plat_ready_msg_process`) then id2
   (`host_ready_msg_process`, payload ≤ 0x63 B) over DR. **No H2D reply.**
2. Arm ETE rings (SR0–2 + DR0–3, depth 32, 8-B nodes, producer = `(idx&0x3ff)|phase<<10`),
   pre-post DR free buffers, keep `out[1]` cleared so firmware precondition P2 can pass.
3. H2D = SR node `{device VA, (len<<16)|0x6d2b}` + HCC frame (12-B header + payload) +
   `pcie_msg_send(chip,3)`: `out[0] 0x40039010 = 1<<3`, then `out[2] 0x400392d4 |= 1`.
4. H2D ids: only **3** (data announce) and 5 (failure-branch receive-buffer check; device id5 =
   `BUG()` trap — never post 5). H2D message ids live in the HCC header, not the mailbox.
5. D2H mailbox: ack `out[3]=0x40101438=1`, drain `out[1]=0x40039014=0`, rearm
   `out[4]=0x40101414=1`, dispatch lowest bit ≤10; ids 1(drop)/3(DR drain)/6,7(wake) registered.
6. ETE registers: SR group A `+0x10 base/+0x14 depth−1/+0x18 producer/+0x08 ctrl`; DR group B
   `+0x30 base/+0x34 depth−1/+0x38 free-buffer producer`; device writes `+0x1c`/`+0x3c`;
   `+0x00 enable` is firmware's; intr/mask block CA 0x40039508 (`&=0xffe0f8f8`).
7. D2H HCC tables (all in REPORT §5.5): plat g4 (0–4); hdpp chip g3 {0,4,5,7,8} + core g0
   {0,1,2,3,4,7,8}; hmac chip g1 (28), g2 (12), g3 (8); wal chip g2 (19).
8. hdpp/alg attach: hcc queue customers chip **queue 2** / core **queue 0** (lock/unlock), hdpp
   thread + object init, config/alg/cali responses via `hmac_event_config_syn` (chip g1 id3).
9. Post-ready init: `multi_chip_loading` → `hal_main_init` → `hmac_main_init` (tables, board/fsm,
   cali file save, hcc state=4) → `wal_main_init` (tables, cfg80211, state=5); no H2D in between.
10. B3 bench probes P0–P5 (REPORT §9): arm all rings, watch for `out[1]` bit6/bit3 after draining
    0x4, snapshot DR `+0x3c` per channel, decode the group-4 ready payloads. Never write
    0x400392f0.

## Artifacts

- Report: `REPORT.md` (791 lines, evidence-marked [V]/[V*]/[I]/[U], regeneration appendix).
- Vendor modules disassembled (local, not committed): plat.ko md5 `f688618b…`, wifi.ko md5
  `1745ef77…`, FIRMWARE.bin sha256 `7fc87e20…`.
- No device contact; no vendor binaries/blobs committed.
