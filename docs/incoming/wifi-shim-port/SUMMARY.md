# SUMMARY: Wi-Fi binary-port lane verdict

**VERDICT: NO-GO** - the vendor's unmodified 5.10.201 Wi-Fi binaries cannot run
on our 6.18.60 image via a compatibility shim. The import layer is shimmable
(16 symbols, prototype compiles); the struct-layout layer is not (97/153
probed member offsets drifted; disassembly-verified live-path hits).

## Milestone numbers

- M1 IMPORT CLOSURE: plat.ko 126 UND / wifi.ko 308 UND, distinct 367.
  (a) 6.18 kernel: 174 (148 plain + 26 same-name-risk). (b) vendor .ko closure
  (depth<=3): 177 - plat.ko provides 138 to wifi.ko, cfg80211.ko 26, ksecurec
  11, hi_kpie 2; order hi_basic->hi_kpie->ksecurec->compat->cfg80211->
  [hi_pcie]->plat->wifi. (c) drifted wireless API: 0 unresolved imports (no
  mac80211 use); the drift is structural (M3). (d) vendor hi_*: all satisfied
  by vendor modules except done_path_create (stub). (e) other: 15 shim symbols.
- M2 VERMAGIC: CONFIG_MODVERSIONS/SIG/FORCE_LOAD all off -> modprobe
  --force-vermagic impossible; local-copy .modinfo patch designed + TESTED OK
  (tools/vermagic-patch.py, 4/4 modules; "6.18.60 SMP mod_unload ARMv7 p2v8 ").
- M3 LAYOUT (decisive): 97/153 drifted (net_device total reorganization;
  sk_buff +8 block; genl_info nlhdr/genlhdr/extack). Census: wifi.ko 21,
  cfg80211.ko 20, plat.ko 4 drifted KERNEL-owned member hits; >=12 verified
  real on live paths (ndo_set_mac state@0x28, mtu@0x148 write, skb head/tail/
  end/mac_header/users, nl80211 doit writes genl_info.genlhdr@0x0c = memory
  corruption on first nl80211 command). Vtable order: netdev_ops 80->92
  (insertions at 12-15) but driver populates only slots 2/3/4/9/16/19 ->
  partial, non-fatal; ethtool_ops unregistered -> generic; genl_ops first 7
  same order -> family registers, bodies die.
- M4 SHIM: wifi_shim.c + Makefile + README committed; exports all 16 drifted
  symbols on 6.18 equivalents; COMPILE-OK against the 6.18 built tree.
  cfg80211_ops translate-at-registration designed for the 6.18-native-stack
  alternative but non-viable (wiphy.priv -64, wiphy.registered -136, wdev.conn
  -56) - documented.
- M5 VERDICT: NO-GO. 367 = 174 (kernel) + 177 (vendor modules) + 16 (shim);
  blocked-by-struct-layout = the 3b verified set + genl_info corruption +
  hrtimer rework + mem_section (SPARSEMEM->FLATMEM). Bench sequence to
  empirically confirm (3 falsifiable NO-GO checkpoints) is in REPORT.md M5.

## Blocker list

1. net_device layout drift (all probed members moved; verified live hits).
2. sk_buff +8 drift on data-path fields (driver + plat + cfg80211).
3. genl_info drift: nl80211 handlers corrupt 6.18 netlink pointers.
4. vendor cfg80211.ko (mandatory - our kernel has no wireless stack) is itself
   5.10-layout code on 6.18 core structs.
5. hrtimer struct rework (hrtimer_setup; driver writes function@5.10 offset).
6. mem_section: FLATMEM 6.18 has no sparse section array.
7. vtable order drift - partial (survivable for this driver's populated slots).
8. 16-name symbol drift (all mapped in the shim draft).

## Files

REPORT.md (full), analysis/import-closure.csv (367 rows),
analysis/struct-layout-drift.md, analysis/vtable-order-drift.md,
analysis/member-audit-summary.md (+ per-module CSVs), analysis/vermagic.md,
shim/ (wifi_shim.c, Makefile, README.md), tools/ (regeneration scripts).

Commit: <FILLED-IN-AFTER-COMMIT>
