# wifi-5ghz-bringup - 5 GHz endpoint readiness + one-radio/two-band wiphy (REPORT)

**Lane:** wifi-5ghz-bringup (shared task **task-154**), HOST-ONLY.
**Written:** 2026-11-25. **Status:** analysis complete, patch chain + host tests
delivered, module built and host-verified; **NOT FIRED** (the Lead owns the
bench/recovery door). Writes restricted to
opensource/docs/incoming/wifi-5ghz-bringup/.

**Base module under test:** the FINAL ASSOCIATION module
(/tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko, 344,372 B, sha256
ab9e18a4..., 56 params) = all-patch 5b587949 + wpa3-sae + ap-mgmt +
sta-ops + data-tx + the corrected mgmt-TX carrier. This lane's module adds the
5 GHz profile and the fix below: **57 params**, stripped **351,008 B** sha256
1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53.

---

## 0. TL;DR

1. **The 5 GHz-fire diagnosis in the ledger (2026-11-29 07:00) is refuted.**
   "Endpoint 0001 needs its OWN ready handshake/boot ladder" is not what the
   hardware is: both PCI functions enumerate as **chip 0**, their BAR0 windows
   **alias the same on-chip register space**, and the vendor initialises/servics
   **only register instance 0**. A second prepare/upload/release ladder on
   0001 would write the *same* firmware SRAM through the other RC's iATU and
   re-release a running CPU - it must not be run. [V]
2. **Real root cause:** the wiphy registration gate is evaluated against each
   PCI context's **own** HCC core, and only the booted primary endpoint
   (domain 0) ever reaches READY. The 5 GHz wiphy was *allocated* on 0001 but
   *never registered* because nobody can (and should not) boot 0001.
3. **Vendor model:** one radio, two logical bands; the vendor builds ONE wiphy
   per band over the **one** HCC service (WIPHY=2). The two PCI endpoints are a
   transport/host-view topology (cross-wired RCs), not two radios.
4. **Fix implemented:** with band5g=1 the **primary endpoint's wiphy carries
   BOTH bands** (2.4 GHz + the vendor 29-channel 5 GHz profile), the secondary
   host view gets **no wiphy**, and every AP/scan band byte is derived from the
   cfg80211 chandef / requested channels instead of the legacy per-endpoint
   band byte. band5g=0 behaviour is unchanged (bench-validated 2.4 GHz path).
5. D2H routing is now inherently correct for the 5 GHz AP (scan/mgmt events
   are dispatched to the one booted service's wiphy - the same wiphy that owns
   both bands), which a two-wiphy-over-one-service redirect would not give.
6. Host verification: build clean 0 warnings, 57 params, debug-strip keeps
   .text identical, **two independent builds byte-identical**; portable selftest
   **114/114**, patch static checks **20/20**. Fire recipe in
   FIRE-RECIPE.md; **nothing was fired.**

---

## 1. Question and method

The failed 5 GHz fire (ledger mem-entries.md 2026-11-29 07:00) loaded the
band5g module with band5g=1, saw the driver advertise the 5 GHz profile and
allocate the 5 GHz wiphy on 0001:00:00.0, but iw showed only phy0 (2.4 GHz)
and hostapd on the "5 GHz phy" failed with "Could not determine operating
frequency". The recorded hypothesis: run a second HCC ready handshake/boot
ladder on endpoint 0001.

This lane asks: (a) how does the driver register wiphys per endpoint; (b) does
the boot ladder target one or both endpoints; (c) what does the vendor do for
the second endpoint / the 5 GHz band; (d) implement the correct bring-up and a
fireable 5 GHz AP ladder.

Method: read-only source review of the association-module tree (the exact
module under test, plus this lane's patches), the committed phase/ledger
evidence for the two endpoints, and the committed vendor/sibling RE
(phase21/phase32, wifi-5ghz, wifi-cfg80211). No device contact, no firmware
bytes, no vendor binary in any tracked file.

---

## 2. How the current driver allocates and registers wiphys

All file names below are in the association-module source tree produced by
opensource/docs/incoming/wifi-stability/tools/build-association-module.sh
(local build artifact; the tree is reproducible from the committed lane patches
and the base module sha256 5b587949...).

### 2.1 One wiphy per PCI probe

* main.c: hi5622_probe() runs per PCI function (both 59e7:0005 endpoints)
  and calls hi5622_cfg80211_init(ctx); each call allocates a wiphy
  (wiphy_new_nm) and picks a band from the PCI domain:
  hi5622_cfg11_pick_band() -> domain 1 = 5 GHz, otherwise 2.4 GHz. The
  domain-to-band mapping was always marked **[I]** (the vendor selects the band
  by command/band byte, not by PCI domain - wifi-5ghz REPORT).
* hi5622_cfg80211.c: hi5622_cfg80211_ready(ctx) is called from the service
  pass (service.c: hcc_service_pass() -> hi5622_cfg80211_ready(ctx)) and
  schedules the one-shot wiphy_register() work item when the **per-ctx**
  gate passes: hi5622_cfg_reg_gate(&c->model, core->boot_latched,
  core->phase, core->plat_ready, core->dev_ready) requires phase >=
  READY_TXQ_SYNC, plat_ready and dev_ready on that context's HCC core
  (hi5622_cfg80211_logic.c). Those flags are produced by the g4 id1/id2 ready
  pair received on that context's D2H path (hcc_core.c).
* Consequence: **registration is a per-endpoint state**, although the radio and
  its firmware are chip-global.

### 2.2 The service, and why a second wiphy has nothing to run on

* Every probe creates its own struct hi5622_hcc_service (host rings + service
  loop + direct D2H reader). The D2H frame dispatch is bound to the *service
  owner's* cfg80211 instance: hcc.c: hi5622_hcc_deliver() -> hi5622_scan_deliver(ctx,...)
  uses ctx->cfg, and the AP/mgmt/scan/keys builders all post through
  hi5622_frame_post(c->ctx, ...).
* So merely registering the 0001 wiphy would **not** make it usable: its
  ctx->hcc never reaches READY/ACTIVE (every post is fail-closed), and D2H
  events (scan results, mgmt RX) are delivered to the booted context's wiphy,
  not to it.

### 2.3 Failed-fire re-read (what actually happened)

* Both wiphys were allocated at probe: "wiphy allocated for endpoint
  0000:00:00.0 (band 2.4GHz ...)" and "... 0001:00:00.0 (band 5GHz ...)"
  [V, mem-entries.md 2026-11-25 15:00].
* The boot ladder + C2 ladder ran on **0000 only** (the documented recipe) and
  registered phy0; the ready handshake decoded on 0000 [V, same entry].
* The 5 GHz wiphy's gate therefore evaluated boot_latched/phase/plat_ready/
  dev_ready on the **0001** context: permanently false -> never registered.
* iw saw phy0 only, hostapd 5 GHz refused [V, mem-entries.md 2026-11-29 07:00].
  This is a **gate-scope bug**, not a missing hardware service.

---

## 3. Why endpoint 0001 must not get its own ladder (the decisive evidence)

The board's two endpoints are **two host views of ONE chip**, and the driver
already drives the one service instance. Evidence:

| fact | evidence |
| --- | --- |
| Both endpoints report **chip 0**: vendor live log [PCIEL]chip 0, bus 0, probe cnt 1, phy_devid:1 (0000) and [PCIEL]chip 0, bus 1, probe cnt 2, phy_devid:0 (0001) | opensource/docs/phase21/sibling-ep.md:90-91 [V] |
| Two register instances exist 0x800 apart (0x40039000 / 0x40039800) but **only instance 0 is initialised/serviced** by the vendor | phase21/sibling-ep.md:155,195 [V] |
| Both functions' BAR0 windows **alias the same on-chip register space**: identical reads of the same device CAs through 0x403b8000 (ep0) and 0x583b8000 (ep1) | phase21/sibling-ep.md:198 (A.4) [V] |
| The vendor's message path/rings are instance 0; the live INTx (209) is ep1's, but the block is the same one our driver programs through ep0 | phase21/sibling-ep.md A.3/A.5, phase32/doorbell-endpoint-path.md:18-21 [V] |
| Only one firmware image is uploaded; the boot ladder writes it to the chip's shared SRAM through the claimed endpoint's iATU (prepare/upload/verify/seed/release) | transport lane evidence; the same register space to both RCs |
| Both endpoints alias the same ETE/glue registers; a second prepare would reprogram the **same** device CA through a second RC's iATU | phase21/sibling-ep.md:198 [V] + transport lane [V] |

Therefore a second boot ladder on 0001 is a second write path to the one
service, not another service: it would re-upload the firmware over the running
image and re-release a running CPU through the other RC. That is exactly the
"stuck exception / page lost / needs physical power-cycle" class already
recorded on the bench (mem-entries.md 2026-11-29 08:00 bench status: chip in a
stuck exception state, boot ladder not_released, requires power-cycle).

The driver already recognises the "already booted chip" hazard at probe with a
read-only handshake-page check (chip_booted_at_probe in main.c) that refuses
boot-stage writes, but that check only sees a *previous module load*: a
second-endpoint ladder within one load is the same class of mistake and must be
excluded by design/documentation.

---

## 4. What the vendor actually does for the "second endpoint" and 5 GHz

* The vendor keeps **two logical bands on chip 0** (hmac_device_add_band,
  g_ast_band stride 0x2330), builds **one wiphy per band** (WIPHY=2) and
  selects the band by the AP-start channel message band byte and by its
  user-space band commands - **not** by PCI domain
  (opensource/docs/incoming/wifi-5ghz/REPORT.md, committed 2026-11-24).
* The vendor driver services **one device instance**; its per-function device
  array is keyed by phy_devid from config-space cfg[0xff8]&0xf, and
  chip->dev[0] = 0001:00:00.0 is the one used for the message path
  (phase32/doorbell-endpoint-path.md:18-21). Both endpoints are the same chip.
* So the vendor's per-band wiphy split is a **software split over one radio**.
  A second firmware boot per endpoint does not exist in the vendor at all.
* Our "one radio per endpoint" comment in hi5622_cfg80211.c was a clean-room
  design assumption; this lane corrects it (the code comment is updated by
  0002).

---

## 5. Implemented fix

### 5.1 One-radio/two-band wiphy plan (portable, host-tested)

New helper in hi5622_band5g.c/.h:

* hi5622_band5g_wiphy_bands(band5g_enable, pci_domain, out_bands, max, out_n)
  * band5g=0: legacy per-endpoint model, **unchanged** (domain 1 -> {5 GHz},
    else {2.4 GHz}). This is exactly hi5622_cfg11_pick_band()'s auto rule.
  * band5g=1: domain 0 -> **{2.4 GHz, 5 GHz}** on one wiphy; any other
    domain -> **no wiphy** (log: "SECONDARY host view of the same Wi-Fi chip -
    no wiphy allocated here").
* hi5622_band5g_freq_band(freq): 2.4/5 GHz band of a centre frequency from the
  two vendor tables (scan-request band derivation).

hi5622_cfg80211.c is refactored so bands are installed by
hi5622_cfg11_setup_one_band(c, band) in a loop over the plan, instead of one
hard-wired c->band. The wiphy registration gate itself is **unchanged**: one
booted service registers the one radio once, and that radio now exposes both
bands. c->band remains the legacy primary byte (2.4 GHz) for logging and the
2.4 GHz profile; nothing in the band5g=0 path changes.

### 5.2 Band bytes derive from the cfg80211 request, not from c->band

Two functional misses would otherwise break a 5 GHz AP on a dual-band wiphy:

* hi5622_scan.c AP path: the VAP add (g0 id7 user/VAP base), the VAP base and
  the post-ap_cfg VAP start now use **p->band** (from set->chandef.chan->band,
  already computed by hi5622_scan_fill_ap_start) instead of c->band. For a
  ch36 AP that is band **1**; the 2.4 GHz path is byte-identical to the
  bench-validated behaviour.
* hi5622_scan.c scan path: the scan-request band (and the scan's VAP base
  band) is derived from the requested centre frequencies via
  hi5622_band5g_freq_band() (hi5622_scan_batch_band()); a mixed 2.4+5 GHz
  cfg80211 request keeps the first channel's band and logs - the firmware scan
  request carries one chip_id.
* The user_add at interface-creation time still carries the legacy wiphy band
  byte because no channel exists yet; the AP band is carried by the subsequent
  VAP add/channel-config frames. Whether the firmware requires the two to agree
  is **[U]** and is the first thing the fire records.

### 5.3 Why this and not the two-wiphy-over-one-service design

A two-wiphy design (register 0001's wiphy by sharing the primary HCC service)
was analysed and rejected for this lane: it needs a cfg-service redirect through
hi5622_frame_post()/all state checks **plus** a D2H routing table (scan/mgmt
events are bound to ctx->cfg), and it would add a second wiphy whose
association/mgmt RX path is still wrong until that routing exists. The
one-radio/two-band wiphy is what Linux expects for one radio (mac80211 does the
same), gives correct D2H routing for free (the 5 GHz AP lives on the same wiphy
as the booted cfg), and leaves the proven 2.4 GHz path untouched.

---

## 6. Host verification (all performed, no device contact)

| check | result |
| --- | --- |
| Build chain: association tree + sibling carrier patch + 0001 + 0002 | clean, **0 warnings/0 errors** |
| Module contract | **57 params** (56 association + band5g), vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends cfg80211 |
| Debug strip keeps .text identical | PASS |
| Reproducibility | **two independent builds byte-identical**: stripped 351,008 B sha256 13366151..., unstripped 1,342,188 B, .text sha256 b21bbc32... |
| Portable selftest (hi5622_band5g.c) | **114 checks, 0 failures** (98 inherited band5g + 16 new plan/freq->band) |
| Patch static checks | **20 checks, 0 failures** |
| Build script tools/build-5ghz-ready.sh | PASS (rebuilds from committed patches, refuses drift) |
| Host tests tools/run-host-tests.sh | PASS |

Artifact and evidence: evidence/build-artifact.txt, evidence/host-tests.txt.
The .ko stays local (/tmp/wifi-5ghz-ready-artifact/), never committed.

---

## 7. Expected bench outcome and the fire recipe

See FIRE-RECIPE.md. In one line: load the module with band5g=1 + the proven
association params, run the **proven boot ladder on 0000 only** (never 0001),
and the driver registers **one wiphy with both bands**; hostapd
hw_mode=a channel=36 on that phy starts the 5 GHz AP and the external observer
sees the BSS on 5180 MHz.

Two new preconditions the ladder must check:

1. **Regulatory domain.** The kernel's world regdomain marks all 5 GHz ranges
   NO_IR (net/wireless/reg.c, world_regdom in the project 6.18.60 tree [V]), so
   a 5 GHz AP start is refused until a country domain is loaded. The final
   image ships wireless-regdb and hi5622-load already supports
   uci hi5622.ap.country; the recipe sets iw reg set US (or hostapd
   country_code=US) before start_ap and asserts iw reg get.
2. **One AP start per module load** and the inherited stop rules (page golden,
   mask 0; exception -> power-cycle, no retry, never rmmod) are unchanged.

**Expected-FAIL after the 5 GHz AP is on air:** client association still
requires the mgmt/data path and the RF chain (documented project blocker), not
a 5 GHz-specific regression; the rung is the on-air beacon, as in the 2.4 GHz
proof.

---

## 8. Evidence legend and citations

* [V] verified in the repository (live bench log/ledger or disassembly) or by a
  host test in this lane; [I] inferred design reading; [U] unknown, fire-gated.
* mem-entries.md 2026-11-25 15:00 (line 27794) - both wiphys allocated, boot
  on 0000 registers phy0.
* mem-entries.md 2026-11-29 07:00 (line 28126) - the 5 GHz fire and the
  (here-refuted) second-ladder diagnosis.
* opensource/docs/phase21/sibling-ep.md lines 90-91/155/195/198 - chip 0 both
  buses, instance 0 only, BAR0 alias.
* opensource/docs/phase32/doorbell-endpoint-path.md lines 18-21 -
  chip->dev[0] = 0001 and the single message path.
* opensource/docs/incoming/wifi-5ghz/REPORT.md - vendor per-band wiphy model,
  band tables, AP channel band byte.
* Kernel tree net/wireless/reg.c - world_regdom 5 GHz NO_IR rules.
