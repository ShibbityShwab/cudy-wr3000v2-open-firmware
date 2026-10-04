# The Jeton firmware confirms the shared SDK - same luofu_V200, different wrt product (phase 29b, 2026-10-04)

Following up the phase-29 lead, the Jeton AX3000 Core firmware was downloaded, unpacked and
diffed against the Cudy artifacts. The SDK-level identity is now **proven**, not inferred.

## What was fetched

- `https://www.jetontechno.com/product/ax3000-core/` - two firmware images in the page:
  `Jeton-AX3000-CORE_V2.1.00-260819_JETONTECHver.zip` and
  `Jeton-AX3000-Core_V3.1.00-260924_JETONTECHver.zip` (the newer one used here; md5
  `f7e579802fb26eb02b6051cd62acb85a`, 31,987,801 B, containing a 34,079,100-byte `.bin`).
- Unpacked with the project's own `tools/extract_firmware.py` (uImage + UBI + squashfs, no
  modification), then the squashfs with 7-Zip (extraction is complete: 5,073 regular files of
  5,367 inodes; the 407 "errors" are symlinks 7z refuses to re-create, whose targets are already
  extracted).

## The evidence, side by side

| artifact | Cudy WR3000 v2.0 | Jeton AX3000 Core | verdict |
| --- | --- | --- | --- |
| kernel | 5.10.201 | **5.10.201** (`Linux-5.10.201`, load `0x80608000`) | same |
| OpenWrt | 22.03.6 r20265, rev `2.5.24` | **22.03.6-r23439, rev `260924`** | same release, newer build |
| target | `hisilicon/luofu` | **`hisilicon/luofu`** | **identical** |
| arch | `arm_cortex-a9` | `arm_cortex-a9` | identical |
| Wi-Fi blob | `0e530b976d5a20e87358671f1a577695` | **`0e530b976d5a20e87358671f1a577695`** | **byte-identical** |
| Wi-Fi drivers | `lib/hisilicon/ko/hi5622v100_{wifi,plat}.ko` | same paths; wifi md5 `7f1fb14a…`, plat `96e6e9f9…` (newer build) | same tree, newer build |
| DT compat family | `hisilicon,hsan_smp`, `hsan,clk`, `hsan,crg`, `hsan,acp-pcie0/1` | **the same strings** | same platform |
| vendor app layer | `hipriv`, `hi_cfm`, `hi_appm`, `hsan_*` init | **all present** | same SDK |
| module count | ~60 `hi_*` | 286 `.ko` under the same layout | same SDK, fuller set |

## The smoking gun - `/etc/hi_version`

```
Jeton: chip name: luofu_V200   product name: wrt_hg5013_4g_h2   product version: ChenTang_1.2.16.linux0
Cudy:  chip name: luofu_V200   product name: wrt_ax3000_lite
```

**Both devices are builds of the same HiSilicon `luofu_V200` SDK**, different `wrt_*` product
configurations of the same `hisi_trunk`/`opal22` flow (`ulw/phase1/DECISION.md`). Jeton's build
even names the product string **"ChenTang"** - a second vendor line on the same silicon.

## What this means for the blocked source question

1. **Jeton is a second, independent GPL obligor.** GPLv2 obligations attach to whoever distributes
   the binary. Jeton distributes a 5.10.201 kernel, U-Boot, BusyBox and the other GPL components
   built from the shared SDK - and their request address is public (`support@jeton-tech.ru`).
2. **A GPL tarball from EITHER brand is the shared SDK source** - the kernel patches for
   `luofu`/`hsan`, the build config, and the driver build trees. That is the entire from-source
   path this project is blocked on.
3. **Even without source, Jeton's image is a second, newer data point** on the same silicon:
   their kernel is a raw (uncompressed, `comp=5`-labelled) ARM image with the DT strings embedded,
   and their 286-module set is a fuller inventory of the SDK than Cudy ships.

## Next actions

1. Send the Jeton request (ready-to-send text below, saved as
   `OPEN-SOURCE-REQUEST-JETON.md`).
2. Diff the Jeton kernel image against our Cudy kernel dump (`build/tmp/FIRMWARE.bin` context:
   our kernel dumps live in `dumps/`) - the two `luofu_V200` kernels are siblings, and the delta
   is a first look at the SDK's own evolution.
3. Diff the module symbol tables (Jeton wifi/plat `.ko` vs ours) - the newer build may expose
   symbol renames that settle open naming questions from phases 15-25.

## Symbol-table diff (the SDK lineage, third way)

```
wifi.ko: jeton 5867 syms / cudy 5356 syms / common 5349
         jeton-only: alg_cfg_args_analysis_* (spectral-scan config), ...
         cudy-only : 7 wal_config/hmac_config getters
plat.ko: jeton 1051 syms / cudy 856 syms / common 856   <- CUDY IS A STRICT SUBSET
         jeton-only: bal_port_activate/deactivate/halt, firmware_info,
                     g_al_nvram_init_params, exception_get_*_res
```

Jeton's build is a **superset of the same source tree at a newer revision** - the two devices are
provably siblings of one SDK line, and the Jeton extras (spectral-scan analysis args, BAL port
control) are the direction the SDK is evolving.

