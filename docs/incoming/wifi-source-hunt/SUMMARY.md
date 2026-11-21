# Wi-Fi source hunt — SUMMARY (2026-11-21)

**Question:** existing, license-clean source for the Hi5622v100 (Hi5671YV200 "luofu")
HiSilicon Wi-Fi driver stack, adaptable to a 6.18 kernel. Full record: `REPORT.md` in this
directory.

**Answer in one line:** no public source exists for the exact chip, but two GPL-2.0 sources
of the *same HiSilicon HCC driver family* (`wal_cfg80211`/`hmac`/`mac`/`hcc`/`oal`/`oam`)
do exist and are the adaptation base.

## Best candidate (rank 1)
**OpenHarmony `device_soc_hisilicon` — hi3881v100 Wi-Fi driver**
https://gitee.com/openharmony/device_soc_hisilicon/tree/master/common/platform/wifi/hi3881v100/driver
- GPL-2.0 file headers (HiSilicon copyright; repo LICENSE also marks `common/platform`
  Apache-2.0 — the file headers govern the driver code).
- Contains the `wal_cfg80211_*` family (wal/wal_cfg80211.c), 35+ `hmac_*`, `mac_*`,
  `hcc_*` (hcc_comm/hmac/host/slave/task), 40+ `oal_*`, `oam_ext_if`, `dmac_ext_if`,
  `frw` — i.e. the architecture of `hi5622v100_wifi.ko`.

**Rank 2 (supplement):** hongmengkernel hi1105 wifi (875 files incl. `dpe` data-path
engine + `oam_adapter` + `chip`):
https://github.com/5-Super-Rookie-5/hongmengkernel/tree/HEAD/kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi
(GPL-2.0 kernel tree; unofficial mirror — prefer rank 1 for vendored text.)

## What the find does NOT change
- `hal_*`, `alg_*`, `shuangta_*`, `bal_*`, `pcie_*`, device-side `dmac_*` and the
  firmware blob have no public source; PCIe transport + chip HAL remain original work
  (existing RE: phase42/43/45 maps, PORT-SURFACE.md).
- The 928,920-byte firmware remains closed — ROADMAP option 2/3 unchanged.

## Jeton lead — RESOLVED
Same-SoC rebrand proven (identical `luofu_V200` SDK, byte-identical wifi blob, kernel
5.10.201, "ChenTang" product line). **But Jeton publishes no GPL source** — checked today:
product page + FAQ offer firmware only (newest V3.1.00-261026). Lead is disproven as a
*public-source* route; it survives only as a GPL request target (draft letter exists,
unsent by human decision 2026-10-04).

## Cudy GPL — checked today
Download-center WR3000 2.0 has a GPL tab with no published file. The only public Cudy GPL
archive is the **MediaTek v1** one (`GPL_for_AX3000_mt7981.tar.gz`, Drive id
`1und0klzZ22tw4VvS7hxvMr5BmfwD-o0m`) — not our silicon. GPL-REQUEST.md status stands
(sent 2026-10-01, no reply).

## License hygiene (short)
| candidate | verdict |
| --- | --- |
| OpenHarmony hi3881 driver | USABLE (GPL-2.0) |
| hongmengkernel hi1105 | USABLE w/ provenance caution (GPL-2.0) |
| Cudy / Jeton / Huawei | no public artifact (request channels only) |
| Banana Pi TR6560 BSP | GPL platform tree; wifi = prebuilt .ko only |
| HiSTBLinux SDK dumps | NOT-usable (leak; wifi drv = 3rd-party chips) |
| Hi1101/hi3881 personal mirrors, WS73 | no license — reference-only |
