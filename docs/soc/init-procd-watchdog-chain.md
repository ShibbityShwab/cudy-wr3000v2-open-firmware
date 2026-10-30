# The init → procd → watchdog chain, from the vendor binaries

**Date:** 2026-10-30. **Method:** Ghidra 12.2 headless on `rootfs-2.4.15/squashfs-root/sbin/init`
(158 functions) and `/sbin/procd` (539 functions), plus `objdump` for the instruction-level
literals the decompiler cannot resolve in a PIC binary with no data symbols.

This note explains why our kernel resets 30 s after reaching userspace, and why that reset is a
**symptom** rather than a cause.

---

## 1. `/sbin/init` is a 17.8 KB ELF, and IT sets up the watchdog handover

`/etc/preinit` is only a recursion guard:

```sh
[ -z "$PREINIT" ] && exec /sbin/init
```

The real init is the ELF. Its `.rodata` contains, in order:

```
/sbin/upgraded %s %s      Failed to exec upgraded.
/sbin/procd               Exec to real procd now
PREINIT                   /tmp/.preinit
/proc  /sys  /dev  /tmp  /dev/pts  /dev/shm  /dev/null  /dev/console
WDTFD                     Watchdog handover: fd=%s
WDT failed to write: %m   Ping
```

Instruction-level reading of the watchdog block (`objdump -d`, function at `0x1584`):

```
15f0: ldr  r0, [r5]                 ; the watchdog fd
15f8: blt  16d4                     ; no fd -> skip
1600: ldr  r1, [pc, #1232] @ 1ad8   ; literal 0x80285700
1604: bl  ioctl@plt                 ; WDIOC_GETSUPPORT
1608: cmp  r0, #0
160c: beq  1760                     ; success path
```

**`0x80285700` = `_IOR('W', 0, struct watchdog_info[40])` = `WDIOC_GETSUPPORT`.**

So init: opens the watchdog, queries its capabilities, and **hands the fd to procd through the
`WDTFD` environment variable**, logging `Watchdog handover: fd=%s`.

## 2. procd is the actual feeder

`/sbin/procd` strings:

```
WDTFD
Watchdog handover: fd=%s
/dev/watchdog
Opened watchdog with timeout %ds
Watchdog GETSUPPORT failed
Watchdog does not have CARDRESET support
Watchdog GETBOOTSTATUS failed
Watchdog has previously reset the system
Watchdog did not previously reset the system
WDT failed to write: %m
- watchdog -
```

procd's entry (`FUN_00013b08`) reads `WDTFD` from the environment first thing:

```c
name = "WDTFD";
pcVar1 = getenv(name);
if (pcVar1) { wdtfd = atoi(pcVar1); unsetenv(name); }
...
setsid(); uloop_init();
if (getpid() == 1) { sigaction(...); reboot(0); }   /* reboot(0) = RB_DISABLE_CAD, NOT a reset */
if (getpid() == 1) FUN_00020728();                  /* the pid-1 stage machine */
else               FUN_000149dc();
uloop_run_timeout(0xffffffff);                      /* the event loop - runs forever, BY DESIGN */
```

**`uloop_run_timeout(0xffffffff)` is procd working correctly.** procd is an event loop; it never
exits. So "procd is still running" is not a symptom of anything.

## 3. The conclusion that matters

**The watchdog is fed by procd.** Therefore our kernel's 70 s reset decomposes as:

```
our kernel boots -> ubi0 attaches -> ubiblock resolves -> mount_root mounts
  -> /sbin/init execs (C9C = 0x1EB70F)
  -> init hands the watchdog to procd
  -> procd never reaches its event loop (or never pings)
  -> hardware watchdog expires at 30 s
  -> vendor boots, answers at ~70 s
```

**The watchdog was never the disease. It is the mechanism that surfaces procd not finishing.**

Two consequences that cost real time before this note existed:

- **Petting the watchdog from the kernel does not fix anything.** It keeps the board alive while
  procd is stuck, which is useful *as a debugging shim* and nothing more. Any claim that the
  watchdog fix "fixed the boot" is wrong.
- **The 48 s vendor loop and the 70 s our-kernel reset are the same mechanism** — an unfed
  watchdog — differing only in which kernel is running when it expires.

## 4. Why a userspace feeder conflicts

The vendor image already runs its own daemon:

```
root 123  watchdog -T 30 -t 8 /dev/watchdog
```

`-T 30` is the timeout, `-t 8` the ping interval. A second process opening `/dev/watchdog` gets
`EBUSY` (one holder per watchdog device); whichever loses the race may be the one that was pinging.
`/usr/bin/wdfeed.sh` was added in an earlier session and did exactly that. **It has been removed
from `/etc/rc.local`.** Under the vendor, `watchdog` is the feeder; under ours, procd is.

## 5. procd's pid-1 path is a six-stage state machine

`FUN_00020728` dispatches through a jump table Ghidra could not recover:

```
uVar1 = *(state) - 1;
if (uVar1 < 6) (*(code *)(uVar1 * 4 + 0x1fd74))();   /* 6 handlers */
else ulog(3, "...");
```

Six function pointers sit at `0x1fd74`. Recovering them is the next step: the stage that does not
complete is where procd blocks, and that is the remaining question in the boot chain.

## Reproducing

See `tools/ghidra/README.md`. Ghidra 12.2 requires **JDK 25**; the decompiler is calibrated against
this project's hand reversal of `hi_wdt_hw_set_timeout` (all three `bfi` paths reproduce, including
`(secs*2-1) << 10` for `hwver 1`).
