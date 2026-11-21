# M1a PROOF: native CPU delivery works with the mirror OFF (2026-11-21)

Status: **PROVEN on hardware**. Image 5800c9ab58d98dd02b6f621a189ea20a58d4450a6fb4f7c407d8771784712513
(built from the adopted release tree with only cmdline changes plus one probe-only init script).

## What was different from the negative E0-a test

- E0-a (image 990c5cab): "luofu_switch.cpu_mirror=0" only -> IP dead, native completions were
  status=37 err=1 (exception-class frames dropped).
- M1a proof: same mirror-off cmdline **plus** "luofu_napt_init=1" and a boot-time replay of the
  decoded vendor hi_pfe_napt_init sequence (the probe-only /etc/init.d/luofu-napt-init, see
  ../luofu-napt-init.sh) and "luofu_pie.rxdiag=1".

## Proof evidence (build/evidence-bench-gates/m1a-proof/, gitignored)

- boot: 6.18.60, 2 CPUs, GIC-0 29 Level twd, no Oops/BUG/Kernel panic; selector 0x11;
  /proc/cmdline contains luofu_switch.cpu_mirror=0 and luofu_napt_init=1;
  dmesg "luofu-napt-init: applied (up_port=0x10000034 bases=0x80400000/0x80408000)".
- network **with no mirror**: host ping 200/200, 0% loss, plus working SSH to 192.168.1.1.
- native RX path: UART "pie-trace RXDIAG ch=0/1 ... status=77 ... port=11" frames delivered to
  the stack (status 77 class accepted; the E0-a status=37 drops are replaced).
- counters: NAPT init registers read back stock values (pfe_glb+0x2c=0x10000034,
  napt+0x64=1, napt+0x130=0x80400000, napt+0x50=0x00f00b69); eth2 rx 51648 at capture time.
  LEDs off, event0 present.

## How the image was built / installed

- Build clone: canonical adopted config-6.18 with luofu_switch.cpu_mirror=1 -> 0, cmdline appended
  "luofu_napt_init=1 luofu_pie.rxdiag=1"; probe init script added under
  target/linux/hisilicon/base-files/etc/init.d/luofu-napt-init (0755). Clone restored to canonical
  after the build; this directory keeps the exact script + command line.
- Installed over the broker channel from a consistent stock boot (kernelb mtd12, rootfsb mtd14,
  fw_setenv bootflag=b read back, boot_reg=0x11 read back, FIRE_OK), then rebooted.

## Why this matters

The missing piece for native forwarding was the flow-engine (NAPT) initialization, not the
readable LSW windows: the W10 IFC defaults already punt broadcast/ARP to the CPU and route unicast
into NAPT; with NAPT uninitialized unicast died there. Replaying the vendor init makes the engine
punt unicast to the CPU, so the CPU-copy mirror workaround is no longer required for eth2 (M1a).
Next: move the sequence into the driver (W22) instead of a userspace probe script, then M1b
(per-port delivery / LAN-to-LAN) and the multi-port hardware bridge.
