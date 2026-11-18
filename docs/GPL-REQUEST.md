# GPL source request: Cudy WR3000 v2.0 (HiSilicon luofu)

**Status (2026-11-18): SENT 2026-10-01; STILL NO REPLY AND NO EVIDENCE COPY IN THE
REPOSITORY.**

The parent ledger (`mem-entries.md`), `PORT-SURFACE.md` and `PORT-PLAN.md` record the
request as sent on 2026-10-01. The channel recorded in the parent
`TIMELINE-AND-WAY-FORWARD.md` is email to `support@cudy.com`. The repository does **not**
hold a copy of the sent message, a delivery receipt, or the reply - that is an evidence gap
to close (see "Evidence trail"), not a reason to call the request unsent. The Jeton (same-SoC)
letter is the separate obligor letter and remains an unsent draft (`JETON-GPL-EMAIL.txt` in
the parent repository).

**Do not describe this request as "draft, ready to send" in other documents.** It was sent.
The *follow-up* message below is a draft until the sent copy and a receipt are saved in this
directory and the tracking table is updated.

**Publication status of this repository: NOT READY.** This file documents a legal request and
adds no vendor content, but the published half still has an open history purge (ten vendor
`.o` files removed from HEAD on 2026-11-17 but still in published history). See
`../README.md`, `../NOTICE.md` and `PUBLISH-READINESS.md`. Nothing in this file
changes that status.

---

## Contact and channel facts checked for this follow-up (2026-11-18)

"verified-by-web" means the URL was fetched on 2026-11-18 and the quoted string was read from
the returned page; the URL is recorded so the check can be repeated. "repo-recorded" means the
claim exists in a parent-repository file. Anything else is marked **NEEDS-VERIFICATION** and
must not be used in a letter until it is checked.

| detail | value | status / source |
| --- | --- | --- |
| Cudy support email | `support@cudy.com` | repo-recorded (parent `TIMELINE-AND-WAY-FORWARD.md` section 2, and section 4 item 7 for the escalation plan); verified-by-web 2026-11-18 at <https://www.cudy.com/pages/contact> (HTTP 200) |
| Cudy sales email | `sales@cudy.com` | verified-by-web 2026-11-18 at <https://www.cudy.com/pages/contact> (HTTP 200) |
| Cudy phone | 0086 755 8600 8993 | verified-by-web 2026-11-18, same page |
| Cudy postal address | Shenzhen Cudy Technology Co., Ltd., 7F, Lepu Tower (West), 66 Xingke Rd, Nanshan, Shenzhen, 518055, China | verified-by-web 2026-11-18, same page; matches the FCC grantee address |
| Cudy FCC grantee code | `2APRG` (Shenzhen Cudy Technology Co., Ltd.) | repo-recorded in `DOCUMENTATION-HUNT.md` (this directory); verified-by-web 2026-11-18 at <https://fccid.io/2APRG> (HTTP 200). `fcc.report` is bot-gated (HTTP 403 on fetch) - use a browser |
| FCC ID of *this* unit | not recorded in the repository | **NEEDS-VERIFICATION**: read the label on the unit/box and record it before citing an FCC ID |
| Address printed on the retail box | not recorded in the repository | **NEEDS-VERIFICATION**: read it and record it before a postal letter; use it if it differs from the website address |
| Cudy GPL web page | <https://www.cudy.com/pages/gpl> returns HTTP 404 | verified-by-web 2026-11-18; the same conclusion is in the parent `HARDWARE-IDENTITY.md` |
| Cudy Download Center | <https://www.cudy.com/pages/download-center> returns HTTP 200, but the page is script-rendered and a plain fetch contains no product/GPL text | verified-by-web 2026-11-18; whether a WR3000 v2.0 GPL archive exists there is **NOT confirmed** - **NEEDS-VERIFICATION** (browser session) |
| Jeton contact | `support@jeton-tech.ru` | repo-recorded (`JETON-GPL-EMAIL.txt`; `phase29/OPEN-SOURCE-REQUEST-JETON.md` says "listed on the product page"); not re-checked on the vendor site - **UNVERIFIED-by-web** |
| GPL enforcement programme | name only: Software Freedom Conservancy's compliance program | repo-recorded as a plan in parent `TIMELINE-AND-WAY-FORWARD.md` item 7; current intake route/address **NEEDS-VERIFICATION** before contact |

The phone number and the postal address are company-switchboard/legal-entity details, not a GPL
compliance owner. Ask each message for the person or team that owns GPL compliance.

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

---

## The original request (canonical text)

The canonical text of the 2026-10-01 request follows. The repository does not hold the sent copy,
so this is the record of record rather than a byte-exact reproduction of the message that left on
2026-10-01. If it is re-sent, save the sent copy + receipt here and update the tracking table. Do
not silently rewrite it - the vendor has (or is entitled to) this version.

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

---

## Request tracking

| date | action | channel | evidence | status |
| --- | --- | --- | --- | --- |
| 2026-10-01 | original GPLv2 3(a)/3(b) request sent | email to `support@cudy.com` (repo-recorded address; web-verified 2026-11-18) | parent `TIMELINE-AND-WAY-FORWARD.md`; parent `mem-entries.md` 2026-11-17 00:20 audit; `PORT-SURFACE.md`; `PORT-PLAN.md`; `PUBLISH-READINESS.md` item 3 | **SENT** - sent copy, receipt and reply not held |
| 2026-10-09 | Jeton (same-SoC, separate obligor) letter rewritten; still a draft, not sent | draft email to `support@jeton-tech.ru` | `JETON-GPL-EMAIL.txt` (parent repo, changelog); `../NOTICE.md` | **DRAFT - NOT SENT** as of 2026-11-17 |
| 2026-11-17 | status reconciled as "sent, not draft"; evidence gap recorded | repo edit | `PUBLISH-READINESS.md` item 3; published-half commit `fbd4a72` | **RECORDED** |
| 2026-11-18 | follow-up message + escalation ladder written (below) | this file | parent `mem-entries.md` 2026-11-17 00:20; web checks above | **READY TO SEND** - not sent |
| (pending) | reply, delivery receipt, or auto-reply | - | save here | **NOT RECEIVED** |

---

## Follow-up message (ready to send; not sent as of 2026-11-18)

Send from the same address as the original request where possible, plain text, no attachments.
Store the sent copy and any auto-reply here, then update the tracking table and append a parent
ledger entry. Before sending, re-check the two addresses above (a web page can change).

> To: support@cudy.com
> Subject: Follow-up - GPLv2 source request for Cudy WR3000 v2.0 (HiSilicon Hi5671Y, board R116), original request 2026-10-01
>
> Hello,
>
> I wrote to you on 2026-10-01 at this address asking for the complete corresponding source code
> for the GPL-licensed components in the Cudy WR3000 v2.0 firmware (OpenWrt 22.03.6 based,
> Linux 5.10.201, board R116, HiSilicon Hi5671Y "luofu"). I have not received a reply. This is a
> polite follow-up; the request itself is unchanged.
>
> The device distributes 58 loadable kernel modules under /lib/hisilicon/ko that declare
> license=GPL, plus the GPL parts of U-Boot 2022.07, BusyBox, opkg and the OpenWrt 22.03.6 base.
> Under GPLv2 section 3, distributing these binaries obliges you to provide their corresponding
> source, either as an archive/download link or as a written offer valid for at least three years.
>
> For identification, the unit reports Cudy firmware WR3000V2-R116-2.4.15-20251030-114751,
> OpenWrt 22.03.6, Linux 5.10.201 #0 SMP Sat Nov 25 18:18:57 2023, target hisilicon/luofu.
> I can provide the exact module list and md5 sums on request.
>
> Please confirm receipt and tell me which of the following you can provide and when:
>
> 1. the complete Linux 5.10.201 kernel tree used to build this image, with its .config, the
>    board device trees (base DTB and the board overlays) and the build scripts for the stock
>    image;
> 2. the sources of the GPL-declared modules under /lib/hisilicon/ko, including the
>    hi_kcfe_*/hi_lsw_*/hi_swa/hi_kpie/hi_kmac/hi_kmdio/hi_kphy/hi_basic/ksecurec/hyfi_bridging
>    modules and the rest of that directory, together with their Makefiles, Kconfig entries and
>    headers;
> 3. the vendor's Linux bridge patch series and any modified kernel headers the modules build
>    against - in particular the vendor sk_buff layout and the br_* hook pointers, which are the
>    reason those modules cannot run unmodified on a kernel without the vendor patch set;
> 4. the U-Boot 2022.07 sources and board configuration, including the FIP/bootfip packaging
>    tooling, the 40,732-byte FIP/DTBO trailer generator, and the recovery/failover logic;
> 5. the source or build recipe for /lib/firmware/hi_wifi/FIRMWARE.bin and the two configuration
>    files beside it, or a written statement of their license and redistribution terms;
> 6. the GPL userspace components shipped in the image (hsan_* init scripts, hi_appenv/hi_*env
>    tools, LuCI modifications and the hisilicon/luofu target profile);
> 7. for any item you believe is not GPL-licensed, a written statement naming the item, its
>    license, and where its source can be obtained.
>
> I am happy to receive the source as an archive, a download link, or a written offer, and I will
> confirm the exact firmware version it corresponds to. If I do not hear back by 2026-12-09 I will
> follow up through another channel; I would much rather settle this directly with you.
>
> Thank you for your time.
>
> [name]
> [return address / country]
> [date]

---

## Escalation ladder (planned; nothing below has been executed)

The rungs are ordered cheap-to-expensive and all ask the same thing: source, or a written offer
valid for at least three years under GPLv2 section 3. Do not move to the next rung until the
previous one is documented (date, time, channel, who answered, reference number). Keep every
message factual and civil; the objective is the source, not a dispute. **No agent can send
mail or place calls - rungs 1-6 need the human owner of the unit.**

1. **Resend to Cudy support and ask for the compliance owner.**
   Action: send the follow-up above to `support@cudy.com`, with `sales@cudy.com` copied only if a
   human decides that helps. Ask for a ticket number, the name/team that owns GPL compliance, and a
   date by which an answer is realistic.
   Contact status: both addresses verified-by-web 2026-11-18.
   Evidence to keep: sent message with headers (`.eml` or plain text), ticket number, auto-reply,
   screenshot if a form is used.
   State: **READY (message above) - NOT SENT.**

2. **Phone and web contact form.**
   Action: call 0086 755 8600 8993 and use the form at <https://www.cudy.com/pages/contact>
   (both verified-by-web 2026-11-18). China Standard Time is UTC+8; record the local time of the
   call. Ask the same question: who owns GPL source compliance, and what is the ticket.
   Evidence to keep: date/time, number dialled, who answered, what was promised, form receipt.
   State: **NOT STARTED** (human action).

3. **Registered / postal letter to the legal entity.**
   Action: send the request as a signed letter by registered mail with tracking, to Shenzhen Cudy
   Technology Co., Ltd., 7F, Lepu Tower (West), 66 Xingke Rd, Nanshan, Shenzhen, 518055, China
   (verified-by-web 2026-11-18; same address as the FCC grantee). Use the address printed on the
   unit's retail box instead if it differs - that address is not recorded in the repository
   (**NEEDS-VERIFICATION**: read the box before sending). Enclose the canonical request and a short
   cover note naming the original 2026-10-01 email.
   Evidence to keep: letter scan, tracking number, proof of delivery, reply if any.
   State: **NOT STARTED.**

4. **Seller / importer / distributor route.**
   Action: the GPL distribution obligation attaches to whoever distributed the binary copy to the
   buyer. Send the same request to the retailer/marketplace seller (quote the order number, not
   personal data) and ask them to forward it to their supplier/manufacturer. If the packaging names
   an EU/UK importer, include it - it is a separate responsible entity for CE purposes.
   Contact status: seller identity and contact come from the purchase record, not from this
   repository - **NEEDS-VERIFICATION**.
   Evidence to keep: order reference, sent message, their reply/forwarding confirmation.
   State: **NOT STARTED.**

5. **FCC / CE documentation recipients.**
   Action: use the public filings to identify the responsible legal entity and to show the request
   has been made to the right company. Cudy's FCC grantee code is `2APRG`; the FCC applications
   list Shenzhen Cudy Technology Co., Ltd. at the address in step 3 (verified-by-web 2026-11-18,
   <https://fccid.io/2APRG>). Public filing documents on `fccid.io`; `fcc.report` is bot-gated
   (HTTP 403 on plain fetch), so use a browser. For CE, the EU declaration of conformity / importer
   named on the packaging is the recipient.
   Limits: this rung identifies the correct legal entity and applies documentation pressure; it is
   not itself a source-code channel.
   Contact status: this unit's own FCC ID and EU importer are **NOT recorded** in the repository -
   **NEEDS-VERIFICATION** from the label/box.
   State: **NOT STARTED** (low priority for source, useful before rung 3 or 6).

6. **GPL enforcement organisation / legal advice.**
   Action: after the above is documented as unanswered, hand the evidence pack to a GPL compliance
   programme. The parent `TIMELINE-AND-WAY-FORWARD.md` item 7 names Software Freedom
   Conservancy's compliance program as the intended organisation; its current intake route is
   **NEEDS-VERIFICATION** before contact. Depending on jurisdiction, a lawyer's letter may be the
   better next step - seek advice rather than asserting a legal position here.
   Evidence pack: both sent messages with headers, auto-replies, postal tracking and delivery proof,
   call notes, and the license evidence (58/58 GPL-declared modules, the `strings ... license=GPL`
   output, the GPL components list from the original request).
   State: **NOT STARTED.**

**Parallel lead, not a rung:** the Jeton AX3000 Core is the same HiSilicon luofu reference
platform (parent `HARDWARE-IDENTITY.md`), and its letter (`JETON-GPL-EMAIL.txt`) is a draft that
has never been sent. If either obligor supplies the shared SDK tree, it can satisfy much of this
request for both units - verify any received tree against the module md5s and DTB hashes on record
before trusting it.

Do not fabricate a reply or backdate a send. The tracking table and the parent ledger are the
record, and a reviewer will check them.

---

## Requested artifacts checklist (updated 2026-11-18)

The list is longer than the 2026-10-01 letter because the 2026-11-17/18 bench work established what
the blockers actually are. Items 1-8 are GPL-derived or GPL-declared components and belong in a
section 3 request; items 9-11 are build material and licensing questions, where the fallback ask is
a written statement.

| # | what to request | why the reversal needs it (evidence) | expected license basis |
| --- | --- | --- | --- |
| 1 | **Complete vendor Linux 5.10.201 source tree** as built for this image: the tree, its `.config`, and the vendor-specific patches/exports the modules depend on (the symbol exports they import, the memory model, any modified core code) | The kernel config is a fire-class variable: the 3dh-class no-boot was a `CONFIG_SLAB` + `CONFIG_TRACING` interaction, and the vendor config differs from ours (`CONFIG_ARM_MODULE_PLTS=n`, `CONFIG_WIRELESS_EXT=y`, no `CONFIG_CPU_SW_DOMAIN_PAN`, no TRACING in the booting builds). Struct/ABI work cannot be verified against the wrong tree. Evidence: parent `EVIDENCE-INDEX.md` bench line; `mem-entries.md` 2026-11-17 08:05/08:45/17:45; `VENDOR-STACK-ON-MAINLINE.md` | GPLv2 (kernel) |
| 2 | **Sources of every GPL-declared module** under `/lib/hisilicon/ko`: `ksecurec`, `hi_basic`, `hi_kpie`, `hi_kmac`, `hi_kmdio`, `hi_kphy`, `hi_lsw_pub/dp/pfe`, `hi_kcfe_*`, `hi_swa`, `hi_kpinctrl`, `hi_sysctrl`, `hi_acp`, `hi_efuse`, `hi_flash`, `hi_crg`, `hi_clk_corrector`, `hi_cpufreq`, `hi_wdt`, `hi_kpmupwm`, `hi_kjent_rng`, `hi_kteam`, `hyfi_bridging`, `hi_netif`, `hi_kmsgcenter`, `hi_kipc` and the rest, with Makefiles, Kconfig and headers | The LAN/Wi-Fi userspace contract is in these modules: rootif/port registration (`hi_kmac` -> `hi_kpie` -> `hi_cfe`), CFE interface tables, HSLINK/HYFI behaviour, the WEXT `net_device` layout. Without source the contract has to be re-derived from disassembly. Evidence: `mem-entries.md` 2026-11-17 18:55/20:40; `EVIDENCE-INDEX.md` GPL-obligation row (58/58 `license=GPL`) | GPLv2 declared in each module's `modinfo`; ask for all 58 |
| 3 | **The vendor Linux bridge patch series**: the changes to `net/bridge` that call `br_get_dst_hook`, `br_multicast_handle_hook`, `br_notify_hook` and `br_port_dev_get_hook`; the `net_bridge` cache at `netdev_priv+0x10`; any other patched core code the bridge/HYFI path needs | These four symbols are exported **pointer variables** in the vendor kernel, not functions; `hyfi_bridging` stores callbacks into them (`bridge_get_dst`, `port_dev_get`, `br_notify`, `mc_process`), and the patched bridge that would consume them is absent from our tree. No-op function stubs were ABI-wrong; writable pointer variables load and remove the crash, but without the vendor bridge patches the HYFI userspace contract is still unsatisfied - in the v17 fire `br-lan` was created and destroyed in HSLINK cycles. This is a multi-step port without the patch series. Evidence: `mem-entries.md` 2026-11-17 20:15/20:40 and 2026-11-18 06:50; `EVIDENCE-INDEX.md` bench line | GPLv2 (kernel patch) |
| 4 | **The vendor `struct sk_buff` definition and its private fields/padding**, plus any other modified core headers the modules build against | The measured vendor layout differs from ours: `len` 0xc8 vs 0x54, `data_len` 0xcc vs 0x58, headers 0x106/0x108/0x10a vs 0x8e/0x90/0x92, tail/end/head 0x10c/0x110/0x114 vs 0x94/0x98/0x9c, `data` 0x118 vs 0xa0 (+0x74 from `len`/`data_len`, +0x78 from the headers, with a 4-byte vendor field between `data_len` and the bitfields). `hyfi_netlink_receive` dereferenced `skb_clone()+0x118` and faulted. Evidence: `mem-entries.md` 2026-11-17 20:40; `EVIDENCE-INDEX.md` bench line | GPLv2 (kernel header) |
| 5 | **DTS/DTBO sources**: the base DTB (31,220 bytes, sha256 `947ec62d...`) and both board overlays (4,592 and 4,920 bytes), the overlay generator/fixup scripts, the board_id selection logic, and the DTS for the `hsan,*` nodes | The upstream target needs a submittable DTS and bindings; the `hsan,*` node shapes are load-bearing for U-Boot fixups (a simplified node crashed `bootfip` with a data abort) and for driver probe (`hi_kmdio` crashed on `reset_control` lookup against our DT). Evidence: `soc/vendor-dt-notes.md`; `mem-entries.md` 2026-11-17 18:00/18:55 | GPLv2 (kernel DTS) |
| 6 | **U-Boot 2022.07 source and board configuration**, the FIP/`bootfip` packaging tooling, the fixed 40,732-byte trailer generator, env defaults, the recovery/failover logic and U-Boot's DT overlay/fixup code | The loader accepts only FIP packages (`bootcmd=mtd read kernel${bootflag} ${loadaddr}; bootfip ${loadaddr}`); our slice is a uImage plus the fixed trailer; the trailer cannot currently be regenerated from published material, which blocks the upstream image recipe. Evidence: parent `HARDWARE-IDENTITY.md`; `ROADMAP-OPENWRT.md` phase 5; `PUBLISH-READINESS.md` reproducibility item | GPLv2 (U-Boot) |
| 7 | **Wi-Fi driver sources**: `hi5622v100_wifi`, `hi5622v100_plat`, the `hcc`/`wifidrv` components, and the provider of the `HW_RTOS_NS` symbol namespace (211 namespaced imports) | The Wi-Fi module references 211 symbols in a vendor namespace and **zero** `mac80211` symbols; without the vendor tree the port is blocked at link time, before any driver work. Evidence: `PORT-SURFACE.md` | GPLv2 declared in `hi5622v100_wifi.ko` (`license=GPL`, 3,564,728 bytes, `vermagic=5.10.201`) |
| 8 | **Wi-Fi firmware terms**: `/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 bytes, md5 `0e530b976d5a20e87358671f1a577695`) and the two configuration files beside it - source/build recipe if any, or a written license and redistribution statement | The blob is the other side of the message protocol; the upstream Wi-Fi decision (M4 in `ROADMAP-OPENWRT.md`) needs to know whether a port can redistribute it. Evidence: `ROADMAP-OPENWRT.md` phase 3; `DRIVER-BLACKBOX.md` firmware forensics | license unresolved - ask for a written statement |
| 9 | **GPL userspace sources**: `hsan_*` init scripts, `hi_appenv`/`hi_*env` tools, LuCI modifications, the `hisilicon/luofu` target and device profile, the esbc source, and the GPL packages' source as Cudy distributes them (BusyBox/opkg/OpenWrt base, to the extent modified) | Phase 4 of the roadmap replaces the vendor userspace; the target profile and init scripts also document the board wiring the drivers consume. Evidence: `ROADMAP-OPENWRT.md` phase 4; original request item 3 | GPLv2 (and OpenWrt's own licenses) |
| 10 | **Build scripts for the stock image**: image assembly (uImage + FIP trailer, `ubinize`, sysupgrade), per-package build configuration, and the exact build flow identifiers (the Jeton letter records `hisi_trunk` / `opal22` for the shared platform - ask Cudy which apply) | An open target must be reproducible from source; the trailer/dual-slot details are part of the recipe, not an afterthought. Evidence: `ROADMAP-OPENWRT.md` phase 5; `JETON-GPL-EMAIL.txt` | request as build material; state license per item |
| 11 | **A written license statement for anything Cudy says is not GPL** (firmware blob, calibration data, any proprietary userspace), naming the item and where its source can be obtained | Needed to publish a compliant port and to know what must stay private; also closes the ambiguity if the vendor answers only partially. | licensing question |

If only one shipment is possible, items **1-4** are the set that unblocks the bench line (the
vendor-bridge/sk_buff/port-contract work), and items **5-6** are the set that unblocks the upstream
image. Items 7-8 are needed for the Wi-Fi decision but do not block the wired work.

---

## What we publish and what we keep private

- **Redistributable source** received under a GPL-compatible license goes into the published
  `opensource/` half with the license text, attribution, the exact version, and a note on what it
  supersedes in our own work. Our own reverse-engineering notes stay under the existing MIT/GPL-2.0
  split (`../README.md` "Legal", `../NOTICE.md`).
- **Non-redistributable material** (proprietary, NDA-confidential, or license-unclear parts) stays
  in the private evidence store and is summarised here in our own words. A licensing statement from
  the vendor is what turns an item from "unclear" into publishable or private.
- **No vendor binaries or objects are committed** to the published half: no `.o`/`.ko`, firmware
  blobs, or calibration data. Vendor-derived text (including the DTS renderings under `docs/soc/`)
  stays subject to the publish review before the half is made public. This is repo policy
  (`../NOTICE.md`, `PUBLISH-READINESS.md`); the ten vendor `.o` files removed from HEAD on
  2026-11-17 must still be purged from history before publication.
- **Evidence copies** (sent mail, receipts, delivery proof) are kept here or referenced from here.
  If a message contains personal data, store the copy privately and record the location instead of
  pasting it into a publishable file.

---

## Evidence trail

Required, per `PUBLISH-READINESS.md`: the sent message, the delivery receipt, and the
reply (or a dated tracked non-reply). Current state:

- **Original request (2026-10-01):** no sent copy, no receipt, no reply in the repository. The only
  record is the parent ledger and the parent timeline. This is the single biggest evidence gap.
- **Follow-up (draft above):** not sent. When it is sent, save the message with headers, the
  auto-reply, and the ticket number in this directory (or record the private path), update the
  tracking table, and append a dated parent ledger entry.
- **Escalation rungs 2-6:** not started; each rung's evidence requirement is listed with the rung.

When a reply arrives: save it, update the tracking table, append a parent ledger entry, and update
the publish checklists that depend on this file (`PUBLISH-READINESS.md`). Only then change
the status block at the top. If source arrives, keep it in the private evidence store first, check
the license, and publish only the redistributable subset as described above.

## Current next action

The only live action on this file is human: send the follow-up (section "Follow-up message") and
record the result. No agent can send email or place calls. If there is no reply by 2026-12-09,
continue the ladder at rung 2, then 3-5, then 6. If no answer arrives at all, the request and the
unanswered-request record are still the good-faith evidence a reviewer or a lawyer asks for, and
the roadmap's documented-blob fallback applies to the Wi-Fi decision. The artifact request itself
(section "Requested artifacts checklist") is what the reversal needs, regardless of which rung
produces an answer.
