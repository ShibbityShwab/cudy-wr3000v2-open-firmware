# GPL source request: Cudy WR3000 v2.0 (HiSilicon luofu)

**Status: draft, ready to send.** This is the request that turns the Wi-Fi blocker (and every other
binary module) from a reverse-engineering problem into a compliance question. Send it to Cudy support
and to their GPL contact, and keep the reply in this repository as evidence.

---

## Why this request is binding, not a courtesy

Every vendor kernel module in the stock image declares itself GPL:

```
$ strings hi_kmac.ko | grep license=
license=GPL
```

That is true for `hi_kmdio.ko`, `hi_kphy.ko`, `hi_kmac.ko`, `hi_lsw_pfe.ko`, `hi_swa.ko` and the rest
of the 58 modules under `/lib/hisilicon/ko`. These modules are linked against the Linux kernel, export
and consume kernel symbols, and ship under a GPL declaration. Under GPLv2 section 3, distributing them
in a firmware image obliges the distributor to provide the corresponding source code to anyone who
receives the image, on the same terms.

The same applies to the GPL'd userspace components in the image (the `hsan_*` init scripts, the
`hi_appenv` environment tools, and the LuCI Lua code we already de-compiled as evidence).

## The request

> To: Cudy support / GPL compliance
>
> Subject: GPLv2 source request - Cudy WR3000 v2.0 firmware (HiSilicon Hi5671Y "luofu"), model R116
>
> I am the owner of a Cudy WR3000 v2.0 router (board R116, HiSilicon Hi5671Y, firmware OpenWrt 22.03.6,
> kernel 5.10.201). Under GPLv2 section 3, I request the complete corresponding source code for the
> GPL-licensed components distributed in your firmware image, specifically:
>
> 1. All kernel modules under `/lib/hisilicon/ko`, which declare `license=GPL` in their modinfo. The
>    list includes, but is not limited to:
>    `hi_basic`, `hi_kmdio`, `hi_kphy`, `hi_kmac`, `hi_kpinctrl`, `hi_kcl`, `hi_crg`, `hi_flash`,
>    `hi_lsw_pub`, `hi_lsw_dp`, `hi_lsw_pfe`, `hi_swa`, `hi_kcfe_pub`, `hi_kcfe_lrn`,
>    `hi_kcfe_lrn_l3`, `hi_kcfe_lrn_l4`, `hi_kcfe_lrn_mc`, `hi_kcfe_mc_list`, `hi_kcfe_lrn_vxlan`,
>    `hi_kcara`, `hi_khslink`, `hi_kteam`, `hi_kpie`, `hi_acp`, `hi_efuse`, `hi_kpmupwm`,
>    `hi_clk_corrector`, `hi_cpufreq`, `hi_sysctrl`, `hi_kjent_rng`, `hi_wdt`.
> 2. The complete kernel source tree used to build the 5.10.201 image, including the board device tree
>    (`luofu-r116` / the WR3000 v2 DTS) and the configuration.
> 3. Any GPL-licensed userspace components in the image, including the `hsan_*` init scripts, the
>    `hi_appenv`/`hi_*env` tools, and the LuCI modifications.
>
> For the Wi-Fi stack (`hi5622v100_plat`, `hi5622v100_wifi`, and the `hcc`/`wifidrv` components), I
> request the source of any GPL-licensed part, and a written statement of the licensing of the firmware
> blob itself.
>
> Please provide the source in the form you distribute it (an archive or a URL), and identify the exact
> firmware version it corresponds to: the image on my device reports the build as OpenWrt 22.03.6 /
> kernel 5.10.201, Cudy firmware for the WR3000 v2.0.

## What we do with the answer

- If the module sources arrive: the clean-room reimplementation in `docs/ROADMAP-OPENWRT.md` phases 2-3
  collapses for those blocks. The drivers can be ported and submitted upstream directly, with the
  vendor's permission structure intact and the GPL obligations honoured.
- If only the Wi-Fi driver arrives and the firmware stays binary: the Wi-Fi stack becomes a documented
  blob dependency, and the rest of the port proceeds as planned.
- If no answer arrives: the request itself is evidence of good faith, and the roadmap's option 2
  (documented blob) is the fallback. Track the dates here.

## Evidence trail

Keep in this repository: the sent message, the delivery receipt, and the reply (or the tracked
non-reply with dates). This record is what an OpenWrt reviewer or a lawyer will ask for.
