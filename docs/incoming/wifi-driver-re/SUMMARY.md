# Host-driver reversal SUMMARY — Cudy WR3000 v2.0 Wi-Fi (Hi5622V100)

**Lane:** `wifi-driver-re` (host-only, read-only, 2026-11-21).
**Full report:** [REPORT.md](REPORT.md). **No device was touched; no vendor binary, firmware
blob or per-unit calibration value is committed; per-unit data is redacted.**
**Builds on** `wifi-thermal-re/REPORT.md` (firmware-load interface + calibration container) —
this lane extends it, does not redo it.
**Evidence markers:** [V] re-verified this lane from a named local artifact; [V*] device-measured
by an earlier attended lane (cited doc); [I] inference; [U] unknown.

## One-paragraph verdict

The Hi5622V100 host driver is a **cfg80211 full-MAC** stack split across two GPL-licensed vendor
modules — `hi5622v100_plat.ko` (PCIe/BAL/HCC/INI/firmware layer, 704 functions) and
`hi5622v100_wifi.ko` (MAC/MLME/cfg80211, 4,247 functions) — over a closed 928,920-byte firmware.
This lane produced the **definitive interface contract**: (1) the complete **cfg80211_ops map** —
39 of 112 callback slots populated, every one resolved to an internal function name + link-time
address; (2) the **H2D/D2H message service** — header struct, dispatch algorithm, registration
mechanism, the full H2D transmit path and D2H receive path, and the D2H **event-message catalog**
(28 MAC events + 12 + 8 chip events + 5 boot messages + 7 hdpp messages); (3) the **mailbox/ETE
ring protocol** — the two doorbells (`0x40039010` pending-mask / `0x400392d4` doorbell /
`0x40039014` D2H bitmap), the exact doorbell write sequence, and the IRQ dispatch; (4) the
**firmware-memory / shared-RAM contract** (consolidated from the decoded firmware-load interface);
(5) the **calibration container** write/read path and the dual `ZZZZ…a5a5a5a5` blobs
(consolidated from phase8); and (6) the **power/link/scan/STA/AP handler chain** plus the exact
SDK symbol families a GPL request must name.

**Honest verdict:** the *functional map and the interface contract are now achievable and, for the
host side, essentially complete*. The 3.6 MB `hi5622v100_wifi.ko` is still **not recompilable
from disassembly** — it has no DWARF, 309 undefined kernel imports (incl. 211 × `HW_RTOS_NS`
namespace tags), and a vendor C MAC/MLME of 4,247 functions; and the device side remains a closed
firmware whose D2H service was never entered from a clean-room takeover (prior lane's unresolved
gate). A clean-room 6.18 port is therefore **still not a realistic partial AP/STA path**; the
shortest route remains **vendor GPL source** (the module declares `license=GPL`), for which this
lane now names the exact SDK symbols/headers. What this lane *does* raise is the **ceiling of the
clean-room line**: the message header, dispatch tables, command/event catalogs, doorbell protocol
and boot handshake are no longer black boxes.

## Key numbers

| item | value |
| --- | --- |
| cfg80211_ops struct | `.data+0xeb0` in wifi.ko; **39 of 112** slots populated (CONFIG_NL80211_TESTMODE off → 2 fewer fields) |
| wiphy creation | `wiphy_new_nm(ops=0xeb0, sizeof_priv=4, name=NULL)` @ `0x1069a8`; `wiphy_apply_custom_regulatory` + `wiphy_register` in `wal_cfg80211_init` @ `0x10686c` |
| cfg80211/wiphy imports | 25 + 1 mac80211 (`ieee80211_get_channel_khz`); 309 total undefined in wifi.ko |
| H2D send entry | `hmac_send_msg` @ `0x46eb4` → `hcc_msg_tx` (plat `0x11bf8`) → queue → `pcie_msg_send` (`0x160f4`) → `pcie_msg_send_irq` (`0x174a8`) |
| D2H receive | IRQ → `pcie_msg_handle` (`0x171f8`) → per-id handler table; dispatch `hcc_msg_process` (`0x1204c`) |
| message header | `type`@0 (low4=group), `bid`@1, `vap_id`@2, `chip_id`@3, `flag`@4, `id`@6 (u16), params@0x0a |
| doorbells | H2D `0x40039010` (mask) / `0x400392d4` (doorbell); D2H `0x40039014` (bitmap) |
| boot D2H catalog | id 0 `oam_rx_post_action_function`, 1 `device_plat_ready`, 2 `host_ready`, 3 `exception_info`, 4 `heartbeat` |
| MAC D2H event catalog | 28 handlers (scan/BA/ACS/DFS/CSI/spectral/TBTT/channel-switch…) + 12 + 8 chip events + 7 hdpp |
| firmware load | CA `0x01240000`, bounce 0x80000, release `0x40000108`=0x5a5a (unchanged from thermal lane) |
| calibration | writers `0xc3f4c`/`0xc3e54`, verifiers `0xc3d3c`/`0xc3c20`, readers plat `0x2e44`/`0x2c94` |

## Effort / verdict table

| block | today | route |
| --- | --- | --- |
| **Host interface contract (ops map + message service + ring/mailbox)** | **[V] decoded** (this lane) | done host-side; the clean-room line now has the protocol it was missing |
| Recompiling `hi5622v100_wifi.ko` from disassembly | **[X] not viable** | no DWARF, 4,247 fns, 309 imports, vendor namespace |
| Clean-room 6.18 AP/STA | **[X] still not reachable** | closed firmware + un-entered D2H service + vendor MAC in binary |
| 6.18 port via GPL source | feasible after source | 10–16 eng-wks (per thermal lane); source request now nameable precisely |
| Calibration container | **[V] decoded** (phase8 + thermal lane, re-checked) | done |

## What a GPL request must name (new)

Ask the vendor for the **Hi5622V100 ("ShuangTaV100"/ChenTang) host Wi-Fi SDK** that produces
`hi5622v100_plat.ko` + `hi5622v100_wifi.ko`, specifically the symbol families
`hmac_*`, `wal_*`, `hdpp_*`, `alg_*`, `shuangta_*`, `hal_*`, `hcc_*`, `pcie_*`,
`bal_*`, `oal_*`, `oam_*`, `mac_*`, `dmac_*` (2,023 hmac / 1,014 wal / 238 hdpp /
239 alg / 147 shuangta symbol strings), plus the `HW_RTOS_NS` kernel export namespace provider
and the `hi_woc_wifi_rtx`/`hi_kcfe_lrn` runtime-`__symbol_get` hooks. Details + header
names in [REPORT.md](REPORT.md) §7.

## Bottom line

The host side of the Hi5622V100 stack is now **mapped at interface level**: every cfg80211
callback, the H2D/D2H message format and dispatch, the doorbell/mailbox protocol, the boot
handshake, and the calibration container are documented with addresses and disassembly. This is
the maximum a host-only, read-only pass can deliver. What remains blocked is not knowledge of the
host interface but (a) the closed firmware's D2H behaviour and (b) the 3.6 MB of vendor MAC code
— both of which resolve only with vendor GPL source, never by recompiling disassembly.
