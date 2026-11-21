# M2: VERMAGIC STRATEGY

Status: DECIDED - in-place patch of the vermagic string in a LOCAL copy of each .ko.
Raw dumps referenced here live in raw/ (gitignored); only this procedure and the
text patch tool are committed. The vendor .ko files are NEVER committed.

## Facts (machine-verified)

| Item | Value |
| --- | --- |
| vendor vermagic (plat.ko, wifi.ko, cfg80211.ko) | `5.10.201 SMP mod_unload ARMv7 ` (29 bytes) |
| our 6.18 vermagic (computed from built tree) | `6.18.60 SMP mod_unload ARMv7 p2v8 ` (37 bytes) |
| config-6.18 CONFIG_MODVERSIONS | **not set** (no CRC gate; confirmed in target config + built .config) |
| config-6.18 CONFIG_MODULE_SIG / _FORCE | **not set** (no signature gate) |
| config-6.18 CONFIG_MODULE_FORCE_LOAD | **not set** (kernel rejects missing/mismatched vermagic; `--force-vermagic` CANNOT work: modprobe strips vermagic -> kernel `try_to_force_load()` -> -ENOEXEC) |
| config-6.18 CONFIG_PREEMPT | PREEMPT_NONE (no `preempt` tag, matches vendor) |
| config-6.18 CONFIG_ARM_PATCH_PHYS_VIRT | y -> `p2v8 ` tag present in OUR string (vendor kernel lacked it) |
| config-6.18 CONFIG_THUMB2_KERNEL | off (no `thumb2` tag) |
| CONFIG_MODULE_UNLOAD | y both sides -> `mod_unload` in both |

## Why --force-vermagic / plain insmod do not work

The kernel itself checks vermagic at load (kernel/module.c check_modinfo, both 5.10
and 6.18). With CONFIG_MODULE_FORCE_LOAD off, a missing or mismatched vermagic is a
hard -ENOEXEC. modprobe --force-vermagic strips the tag, which then takes exactly the
rejected path. Same-name-length patch of `5.10.201`->`6.18.60` is NOT enough: our
string additionally carries `p2v8 ` (5 bytes), so the patched string must be the full
37-byte value.

## Decision

1. Local copy of each needed .ko (never the original, never committed).
2. In .modinfo: rename the old `vermagic=` key to `xermagic=` (same 9-byte key length).
3. Write `vermagic=6.18.60 SMP mod_unload ARMv7 p2v8 \0` over the expendable
   `srcversion=` entry (47 bytes needed; srcversion entries are >=47 bytes in both
   modules; the kernel never reads srcversion - modprobe-only metadata. We load with
   insmod, not modprobe).
4. Verify with readelf -p .modinfo: exactly one vermagic= entry equal to ours, and
   name=/license=/depends= intact.
5. Load with **insmod** (busybox) in dependency order; never modprobe (module index
   still says 5.10.201 and would re-trigger userspace checks).

Alternative (rejected for v1): rebuild the image kernel with CONFIG_MODULE_FORCE_LOAD=y
then modprobe --force-vermagic. Available as fallback; costs an image rebuild + reflash.

Automation: tools/vermagic-patch.py <in.ko> <out.ko>  (idempotent, verifies).

## Test result (host, 2026-11-21)

PATCH OK on all four required modules (local copies under raw/, gitignored):
hi5622v100_plat.ko, hi5622v100_wifi.ko, cfg80211.ko, compat.ko. Each patched
copy shows exactly one vermagic= entry equal to our kernel string and intact
name=/license=/depends= entries. The .modinfo expendable span used per module:
import_ns=HW_RTOS_NS run (plat/wifi, 3 entries = 63 B), version= backports
banner (cfg80211/compat, 96 B). import_ns entries are inert on our kernel
(no namespaced exports in the 6.18 vmlinux ksymtab - verified).