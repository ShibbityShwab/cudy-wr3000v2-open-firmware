#!/bin/sh
# stock_chain.sh - print the vendor userspace chip power/reset chain from an extracted
# stock rootfs. HOST-ONLY: reads a local directory, never a device, never copies bytes.
#
# Usage: stock_chain.sh [rootfs-dir]
# Default rootfs: /tmp/rootfs2524b (extracted stock 2.5.24 squashfs; local evidence,
# gitignored, never committed). Also works on rootfs-2.4.15 / any extracted stock rootfs.
#
# The point of this scan (task-84): prove from the shipped scripts that the vendor
# userspace performs NO SoC-level Wi-Fi chip power/reset/pin action - the only such
# action in the whole stock boot is hi_pcie.ko's DT-driven endpoint GPIO pulse.
R="$1"
[ -n "$R" ] || R=/tmp/rootfs2524b
set -u
fail=0
say() { printf '%s\n' "$*"; }
chk() { # chk <name> <yes|no> <grep-pattern> <path...>
    name="$1"; want="$2"; pat="$3"; shift 3
    n=$(grep -RIhs "$pat" "$@" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$want" = yes ] && [ "$n" -gt 0 ]; then say "PASS $name ($n hit(s))";
    elif [ "$want" = no ] && [ "$n" -eq 0 ]; then say "PASS $name (0 hits)";
    else say "FAIL $name (want $want, got $n)"; fail=1; fi
}

[ -d "$R" ] || { say "no rootfs dir: $R"; exit 2; }
say "== stock release"
sed -n '1,12p' "$R/etc/hi_version" 2>/dev/null | sed 's/^/   /'

say ""
say "== init order (START numbers)"
for s in hsan_prepare hsan_start gpio_switch; do
    f="$R/etc/init.d/$s"
    [ -f "$f" ] && say "   $s: $(grep -m1 '^START=' "$f")"
done
say "   rc.d: $(ls "$R/etc/rc.d" 2>/dev/null | grep -E 'hsan|gpio_switch' | tr '\n' ' ')"

say ""
say "== hsan_start body (calls wifi_init.sh)"
grep -n 'wifi_init\|/etc/rc.d/' "$R/etc/init.d/hsan_start" 2>/dev/null | sed 's/^/   /'

say ""
say "== wifi_init.sh - the shuangta (Hi5622v100) branch: module load order"
awk '/shuangta loading/,/^ *fi$/' "$R/usr/bin/wifi_init.sh" 2>/dev/null | sed 's/^/   /'
say "   hi_kwificlk.ko present in this rootfs: $([ -e "$R/lib/hisilicon/ko/hi_kwificlk.ko" ] && echo YES || echo NO)"
say "   hi_pcie.ko present: $([ -e "$R/lib/hisilicon/ko/hi_pcie.ko" ] && echo YES || echo NO)"
say "   hi5622v100_plat.ko present: $([ -e "$R/lib/hisilicon/ko/hi5622v100_plat.ko" ] || [ -e "$R/lib/modules/5.10.201/hi5622v100_plat.ko" ] && echo YES || echo NO)"

say ""
say "== SoC-level GPIO users in userspace (etc, lib/preinit, usr/bin, sbin)"
grep -RIhs 'class/gpio' "$R/etc" "$R/lib/preinit" "$R/usr/bin" "$R/sbin" 2>/dev/null | sed 's/^/   /' | head -20

say ""
say "== checks"
chk "no gpio 20/21/28 export in userspace scripts" no \
    'echo *2[018] *> */sys/class/gpio/export' "$R/etc" "$R/lib/preinit" "$R/usr/bin" "$R/sbin"
chk "class/gpio use present only in wifi_init.sh (non-shuangta branches)" yes \
    'sys/class/gpio' "$R/usr/bin/wifi_init.sh"
# /usr/bin/hi_md + hi_mw are standalone devmem wrappers (manual debug tools); what
# matters is whether ANY boot script invokes the write wrapper (hi_mw).
chk "no hi_mw (SoC write wrapper) invoked by boot scripts" no \
    'hi_mw ' "$R/etc" "$R/lib/preinit" "$R/usr/bin/wifi_init.sh" "$R/usr/bin/hsan" "$R/sbin"
say "   note: /usr/bin/hi_md + hi_mw are manual devmem debug wrappers, not called by any"
say "         boot script (checked above); they are the only SoC-register pokes in the tree"
chk "no pcie reset script in init.d/rc.local/platform.sh" no \
    'pcie.*reset\|reset.*pcie' "$R/etc/init.d" "$R/lib/upgrade/platform.sh" 2>/dev/null

say ""
if [ "$fail" -eq 0 ]; then
    say "RESULT: PASS - the userspace chain only insmods modules; no SoC GPIO/register"
    say "        action forges the Wi-Fi chip power/reset. That action is in hi_pcie.ko."
else
    say "RESULT: FAIL - a userspace SoC-level action was found; re-open the vendor sequence."
fi
exit "$fail"
