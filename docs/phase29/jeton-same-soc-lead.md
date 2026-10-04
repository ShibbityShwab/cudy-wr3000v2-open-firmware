# The Jeton AX3000 Core: same silicon, different brand - a fresh GPL obligor (phase 29, 2026-10-04)

The single highest-value untried lead from `docs/TIMELINE-AND-WAY-FORWARD.md`, checked immediately.

## What the search confirmed

**Jeton Tech AX3000 Core** is a rebrand of the **same HiSilicon reference design** this project
reverses:

| | Cudy WR3000 v2.0 (ours) | Jeton AX3000 Core |
| --- | --- | --- |
| SoC | HiSilicon Hi5671Y (V200) | **HiSilicon Hi5671YV200** |
| Wi-Fi | Hi5622V100, PCIe, 2 endpoints | **Hi5622v100 FEM** (same chip family) |
| Wi-Fi class | AX3000, 2x2+2x2 DBDC | AX3000 |
| RAM / flash | 128 MB / 128 MB | **256 MB / 128 MB** |
| Ethernet | 4x Hi-GEMAC + LSW | 4x GbE |
| firmware | OpenWrt 22.03.6 (private `hisilicon/luofu` target) | "OpenWrt-based, heavily modified, closed vendor build" |

Sources: Jeton's product page `https://ru.jetontechno.com/product/ax3000-core/` (has a Downloads
section), the iXBT review, and the mysku.club community thread - where **a community OpenWrt port
for this exact board is being discussed** (Russian-language).

## Why this matters more than a search result

1. **A second GPL obligor.** GPLv2 obligations attach to *whoever distributes the binary*. Cudy's
   request is unanswered; **Jeton is a different company distributing the same vendor SDK's
   binaries**, and a request to them is a fresh, independent obligation. The exact request text is
   in `docs/phase25/OPEN-SOURCE-REQUEST.md` - swap the brand and model and it applies verbatim.
2. **The vendor SDK is shared.** Both devices run the `hisi_trunk`/`opal22` SDK for
   `hi5671y`/`luofu` (`ulw/phase1/DECISION.md`). A GPL tarball from EITHER brand contains the
   kernel patches, the `hi_*` driver set and the build config for the same platform - the entire
   from-source path this project is blocked on.
3. **Their firmware is downloadable.** The Jeton downloads page lists firmware images, and the
   memory config differs (256 MB RAM), so their firmware/kernel config is a **second data point**
   on the same silicon - worth diffing against our dumps even without source.

## Concrete next actions (for the new agent)

- Fetch `https://ru.jetontechno.com/product/ax3000-core/` and its Downloads section; look for any
  "GPL"/"source"/"open source" link.
- Download Jeton's firmware image(s) and unpack with
  `tools/extract_firmware.py`; diff the kernel config/modules against the Cudy dumps.
- Search `mysku.club` (the AX3000 Core thread) for anyone who already obtained the source or
  received a GPL response from Jeton.
- Prepare a Jeton GPL request from the Cudy template (brand/model names swapped).

**Risk: none.** This is all fetching and diffing; no device is touched.
