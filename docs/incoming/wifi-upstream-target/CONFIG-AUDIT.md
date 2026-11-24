# Config audit - luofu/config-6.18 (defconfig) for the target image

Status: host-only audit, 2026-11-24. **No symbol was changed in the release-line
config by this session** - the release line needs the bench configuration. What follows
is (a) the proof that the config is in sync/re-derivable in the tree, and (b) the
submission-clean delta a target PR would need.

## What was verified

- make defconfig in the isolated OpenWrt main tree (commit 27663a0, linux-6.18.60) with
  this target installed: **RC=0**, no "configuration out of sync" warning, and the
  board selections are regenerated:

~~~
CONFIG_TARGET_hisilicon=y
CONFIG_TARGET_hisilicon_luofu=y
CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2=y
CONFIG_TARGET_BOARD="hisilicon"
CONFIG_TARGET_SUBTARGET="luofu"
CONFIG_TARGET_PROFILE="DEVICE_cudy_wr3000-v2"
~~~

- luofu/config-6.18 is a **delta against target/linux/generic/config-6.18**
  (311 lines of delta / 567 lines with provenance comments) exactly as OpenWrt emits
  it; it is derived, not hand-edited (the header records the derivation run and the
  idempotency check from 2026-11-18).
- The full image build from this tree (kernel + DTB + FIP slice + rootfs UBI +
  sysupgrade container) succeeded; see BUILD-VERIFICATION.md. That is the practical
  check that no required symbol is missing from the delta.

## What a submission target PR must change (release line -> clean target)

These are **not fixed here** because the same file is the bench release line and the
2026-11-24 checkpoint image was built from it. Track them as the first target-PR
cleanup items:

1. **CONFIG_CMDLINE / CONFIG_CMDLINE_FORCE** currently hardcode
   ubi.mtd=rootfsb ubi.block=0,0 ... plus every attended bring-up gate
   (luofu_switch.arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1 naptinit=1 ifc=1 ext=1,
   luofu_pie.arm=1 rxcfg=1, luofu_gmac.macregs=1). An installable image must derive root
   from the booted slot and must not enable lab gates by default. Options: pass the
   slot via the loader/devicetree chosen node, or keep a target CMDLINE without FORCE
   and without gates. The hardcoded rootfsb is why the image can only be written to
   pair b today (RELEASE-NOTES.md).
2. **CONFIG_DEVMEM=y / CONFIG_STRICT_DEVMEM=n**: bench instrument; must not ship in a
   production target config (the release line uses it for the UART-side readout).
3. **default ARCH_HISI symbols**: 6.18 default-enables many other Hisilicon SoC
   clock/reset/thermal drivers when ARCH_HISI is selected. The config file note records
   the two clean options (explicitly disable, or tighten the Kconfig defaults upstream);
   decide during review; do not ship default ARCH_HISI as-is without that decision.
4. **Package/feed dependency**: the bdinfo nvmem mac-base cell relies on an
   OpenWrt-carried backport (generic pending 804/809-01). Keep it as an explicit target
   dependency, not an undocumented config delta.
5. **Bench diagnostic symbols**: none are forced by this delta today; keep it that way.

## Reproduction

~~~
# isolated tree with this target installed (see target-skeleton/README section 4):
make defconfig
grep -E 'CONFIG_TARGET_hisilicon|CONFIG_TARGET_PROFILE' .config
make -j16 target/linux/prepare target/linux/compile
make -j16
~~~

## 2026-11-24 update: cfg80211 bench module delta (task-104 handoff)

The kernel config was regenerated this session and gained the cfg80211 bench delta
(CONFIG_CFG80211=m + resolved symbols; WIRELESS/RFKILL/WLAN come from
generic/config-6.18). This is a **bench** requirement for the clean-room Hi5622
driver, not an upstream-target default. Consequences for a target PR:

- decide the Wi-Fi stack (cfg80211 full-MAC vs mac80211/backports) and carry
  CONFIG_CFG80211 plus the kmod package dependency in the target/package metadata,
  instead of a bare module symbol;
- the release/image config keeps the cfg80211 block for the bench image; packaging of
  cfg80211.ko is a follow-up (today the bench insmods it from /tmp);
- the signed-regdb/asymmetric-key chain is kept at kernel defaults in the delta; if a
  minimal target config is preferred, set CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=n and
  drop its resolved symbols (bench does not need signed regdb).

The config file is now the emitted OpenWrt delta (the pre-2026-11-24 annotated
per-symbol derivation is history in docs/SKELETON-SMOKE.md). Regenerate with
'yes "" | make kernel_oldconfig' and re-derive a prompt-free build per
BUILD-VERIFICATION.md section 5.
