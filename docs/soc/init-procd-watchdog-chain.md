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

**Recovered 2026-10-30.** Ghidra reported the dispatch as an unrecoverable indirect jump; `objdump`
shows it is a **branch table**, which is why it looked opaque:

```
fd6c: addls pc, pc, r3, lsl #2      ; switch on (state - 1), states 1..6
fd70: b 105d4                       ; default
fd74: b fd8c   fd78: b ff18   fd7c: b 10178
fd80: b 1044c  fd84: b 10478  fd88: b 10574
```

Each handler's strings were resolved from the PIC literals (literal = target - (pc + 8) at the
`add rX, pc, rX` instruction):

| state | entry | log | then |
|---|---|---|---|
| 1 | `0xfd8c` | `- early -` | `/etc/hotplug.json` |
| 2 | `0xff18` | `console` | **`getpwnam("ubus")`** |
| 3 | `0x10178` | `- init -` | `/etc/inittab` |
| 4 | `0x1044c` | `- init complete -` | `respawnlate` |
| 5 | `0x10478` | - | 20-byte buffer setup |
| 6 | `0x10574` | - | `signal(SIGCHLD)` |

**All of it is satisfiable on this rootfs:** `/etc/passwd` carries `ubus:x:81:81:...`, and
`/etc/inittab`, `/etc/hotplug.json` and `/etc/rc.d` all exist. So procd's stages do not fail for want
of files - which is why the hang is NOT in this state machine.

## 6. THE ACTUAL MECHANISM: no `/dev/watchdog` means nothing can feed it

Putting the pieces together: u-boot arms the watchdog (**30 s**), and the feeder is userspace -
`/sbin/init` opens `/dev/watchdog` and passes the fd to procd. **If our kernel has no watchdog driver
bound, `/dev/watchdog` does not exist, init cannot open it, procd inherits nothing, nothing pings, and
the board resets 30 s after userspace starts.**

That is exactly the observed 70 s total (boot + 30 s), and it is why enabling the watchdog DT node
made the boot survive **1500 s** - the device finally existed for userspace to feed.

So both earlier readings were half-right and half-wrong:

- **The watchdog fix was necessary** - without a `/dev/watchdog`, userspace cannot feed a timer that
  u-boot already armed, no matter what procd does.
- **But it is not sufficient, and it is not "the" fix** - a fed watchdog only keeps a stuck system
  alive. The network still has to come up.


## Reproducing

See `tools/ghidra/README.md`. Ghidra 12.2 requires **JDK 25**; the decompiler is calibrated against
this project's hand reversal of `hi_wdt_hw_set_timeout` (all three `bfi` paths reproduce, including
`(secs*2-1) << 10` for `hwver 1`).
