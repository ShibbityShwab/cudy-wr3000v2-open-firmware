# exp-harness: one-shot device experiment harness + batched multi-hypothesis runner (phase 22, 2026-10-02)

Task `st_01a0fbe0`. The per-experiment recipe used by every phase-21 lane (arm self-recovery,
hide the two vendor modules, stage one module, install a one-shot S99 loader, reboot, wait for
SSH, capture dmesg, run the recovery, verify health) is mechanical, and each lane re-derived it
from the phase docs. This phase turns it into two scripts and one contract:

| file | role |
| --- | --- |
| `tools/exp.sh` | one module, one boot, full cycle incl. self-recovery, capture, recovery, health gate |
| `tools/batch.sh` | N hypotheses in ONE boot, per-hypothesis observables folded into one report |
| `tools/batch-hypotheses.example` | the `<label>|<param>` list the batch runner consumes |

The recipe is transcribed from `docs/phase21/sr-trigger.md` Part B and `docs/phase15/unload-patch.md`
section 0/5. **This task did not touch the router**: `--dry-run` is a pure local print, and no
module was staged on the device.

---

## 1. `tools/exp.sh`

```
tools/exp.sh <module.ko> [module params]
tools/exp.sh --dry-run <module.ko> [module params]
```

`<module.ko>` is copied to `/lib/modules/$(uname -r)/<basename>`, the two vendor modules
`hi5622v100_wifi.ko` / `hi5622v100_plat.ko` are renamed `.omo-off`, and a one-shot
`/etc/init.d/omo-<name>` (symlinked as `/etc/rc.d/S99omo-<name>`) insmods it once at boot.
`[module params]` are passed verbatim to `insmod` (`key=value` words; each is validated against
`[A-Za-z0-9_./:=,+@-]` before it is embedded in the device-side loader).

The ordered steps, matching the task brief exactly:

1. **arm the DEVICE-side self-recovery FIRST** - write `/root/recover-exp.sh` and start it
   detached with `start-stop-daemon -S -b -m` (`setsid` does not exist on this build). Nothing is
   staged until the watchdog is confirmed alive.
2. **stage** - md5-check the module, hide both vendor modules as `.omo-off`, copy the module to
   the boot path, install the one-shot loader that self-deletes, `sh -n` the loader.
3. **reboot and wait for SSH** - poll `/proc/uptime` every `EXP_POLL` s with a `EXP_BOOT_TIMEOUT` s
   deadline until a *fresh* boot answers (a fixed `sleep` is never used as the wait).
4. **capture** - `dmesg`, `lsmod`, `/proc/interrupts`, the module's log lines, and any files the
   module wrote (conventionally under `/tmp/omo-<name>/`) into
   `build/register-dumps/exp/<timestamp>/` (raw files plus `evidence.tar.gz`).
5. **recover** - post the done flag, run `/root/recover-exp.sh`: restore both `.omo-off` modules,
   remove the loader, the `S99` symlink, the staged module and the `/tmp` copy, `rmmod` the
   module, then reboot.
6. **verify health** - see the gate below.
7. **print one line** - `EXP RESULT: PASS|FAIL module=... params=[...] evidence=<dir>/`.

It **fails safe**: a failure in any step (or a failed arm) still runs the recovery, then the
health gate, then prints the FAIL summary with the reason.

Env knobs (all optional): `EXP_WATCH` (watchdog seconds, default 1800), `EXP_BOOT_TIMEOUT` (300),
`EXP_RUN_TIMEOUT` (180), `EXP_POLL` (3), `EXP_KVER` (5.10.201), `EXP_EXTRA_STAGE`
(`local:remote` newline list), `EXP_EXTRA_PULL` (remote paths pulled into the tar),
`EXP_DONE_CMD` (remote test for completion, default `dmesg | grep -q 'done ('`),
`EXP_RESULT_LABEL` (default `EXP`).

### 1.1 Recovery contract (device side)

`/root/recover-exp.sh` is generic and knows only the staged module's basename:

```
recover-exp.sh            restore the vendor stack and reboot now
recover-exp.sh --watch    detached watchdog: wait WATCH s, then recover unless the done flag exists
```

- Armed **before any staging** with
  `start-stop-daemon -S -b -m -p /tmp/omo-exp.timer.pid -x /bin/sh -- /root/recover-exp.sh --watch`.
- The one-shot loader **re-arms the same watchdog inside the takeover boot, before `insmod`**, so a
  chip hang at module init is still recovered.
- **Cancel contract**: `touch /tmp/omo-exp.done`, then run `/root/recover-exp.sh`. The flag is the
  only cancellation; the watchdog fires and recovers if it is absent when the timer expires.
- Recovery restores `hi5622v100_{wifi,plat}.ko` from the `.omo-off` names, removes
  `/etc/rc.d/S99omo-<name>`, `/etc/init.d/omo-<name>`, the staged module, `/tmp/<basename>`, the
  capture tar, the done flag, the timer log and itself, then `sync; reboot`.
- Deep recovery (if the box never returns) is unchanged and documented in
  `docs/phase15/unload-patch.md` section 0: the board is dual-slot, boot the other slot from U-Boot.

### 1.2 Health gate (step 6, exact commands)

Run on the recovered boot; **PASS** requires `WIPHY=2`, `IFACE=6`, `CAL_SUCC=1`, `OMO_OFF=0`,
`STAGED=0`, `LOADER=0`:

```
K=$(uname -r)
iw phy   | grep -c '^Wiphy'                                   # must be 2
iw dev   | grep -c 'Interface'                                # must be 6
iwpriv vap0 alg get_2g_power_param                            # must contain [SUCC]
ls /lib/modules/$K/*.omo-off 2>/dev/null | wc -l              # must be 0
ls /lib/modules/$K/<name>.ko 2>/dev/null | wc -l              # must be 0
ls /etc/init.d/omo-<name> /etc/rc.d/S99omo-<name> 2>/dev/null | wc -l   # must be 0
```

### 1.3 Dry run

`bash tools/exp.sh --dry-run build/tmp/hwprobe-art/hwprobe.ko pollms=100 useirq=1` prints the
full ordered sequence below and exits without opening SSH. Host commands are issued through
`.sshwrap/rsh.sh` / `.sshwrap/rscp.sh`; `<TS>` is replaced by the UTC timestamp on a real run.

```
== exp.sh DRY RUN ==
module    : build/tmp/hwprobe-art/hwprobe.ko
module id : hwprobe (staged as /lib/modules/5.10.201/hwprobe.ko)
params    : [pollms=100 useirq=1]
evidence  : /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/
host      : root@192.168.10.1 (via .sshwrap/rsh.sh, .sshwrap/rscp.sh)
watchdog  : WATCH=1800s, boot timeout=300s, run timeout=180s, poll=3s
# NOTHING BELOW IS EXECUTED IN --dry-run.  ssh/scp shape is exact.

== 1/7 arm DEVICE-side self-recovery (MANDATORY, before any staging) ==
# cancels via the done flag /tmp/omo-exp.done; reboots on expiry
$ cat > /tmp/recover-exp.sh <<'OMO_RECOVER'
  #!/bin/sh
  # device-side self-recovery for the omo experiment harness (tools/exp.sh).
  #   recover-exp.sh          restore the vendor stack and reboot now
  #   recover-exp.sh --watch  detached watchdog: wait WATCH s, then recover
  #                           unless the done flag has been posted
  # Armed with start-stop-daemon BEFORE anything is staged; cancelled by
  # touching the done flag and running this script by hand.
  MOD=hwprobe.ko            # staged module basename
  MODNAME=hwprobe    # staged module name (no .ko)
  WATCH="${WATCH:-1800}"
  DONE=/tmp/omo-exp.done
  LOG=/tmp/omo-exp.timer.log
  if [ "$1" = "--watch" ]; then
  	echo "watchdog armed $(date -u) pid=$$ wait=${WATCH}s" > "$LOG"
  	sleep "$WATCH"
  	if [ -f "$DONE" ]; then
  		echo "watchdog cancelled: done flag present $(date -u)" >> "$LOG"
  		exit 0
  	fi
  	echo "watchdog FIRING: no done flag, recovering $(date -u)" >> "$LOG"
  fi
  KVER=$(uname -r)
  cd "/lib/modules/$KVER" 2>/dev/null
  [ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
  [ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
  rm -f "/etc/rc.d/S99omo-$MODNAME"
  rm -f "/etc/init.d/omo-$MODNAME"
  rmmod "$MODNAME" 2>/dev/null
  rm -f "/lib/modules/$KVER/$MOD"
  rm -f "/tmp/$MOD"
  rm -f /tmp/omo-exp-capture.tar.gz
  rm -f "$DONE" "$LOG"
  rm -f /root/recover-exp.sh
  sync
  reboot
OMO_RECOVER
$ scp -O /tmp/recover-exp.sh root@192.168.10.1:/root/recover-exp.sh

# chmod + syntax-check + start the watchdog detached
$ ssh root@192.168.10.1 <<'EOS'
chmod 755 /root/recover-exp.sh
sh -n /root/recover-exp.sh && echo RECOVER_SYNTAX_OK
start-stop-daemon -S -b -m -p /tmp/omo-exp.timer.pid -x /bin/sh -- /root/recover-exp.sh --watch
sleep 1
cat /tmp/omo-exp.timer.log
kill -0 $(cat /tmp/omo-exp.timer.pid) 2>/dev/null && echo WATCHDOG_ALIVE
EOS

== 2/7 stage the module (vendor modules hidden as .omo-off) ==
# local md5 of build/tmp/hwprobe-art/hwprobe.ko = b5b61ee40273a91a131ef40ca4ffbe0b
$ scp -O build/tmp/hwprobe-art/hwprobe.ko root@192.168.10.1:/tmp/hwprobe.ko

# push to the boot path and hide the two vendor modules
$ ssh root@192.168.10.1 <<'EOS'
L=b5b61ee40273a91a131ef40ca4ffbe0b; K=$(uname -r); cd /lib/modules/$K || exit 1
R=$(md5sum /tmp/hwprobe.ko | cut -d' ' -f1)
[ "$R" = "$L" ] || { echo MD5_MISMATCH local=$L device=$R; exit 1; }
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
cp /tmp/hwprobe.ko /lib/modules/$K/hwprobe.ko
sync
echo STAGED; md5sum /lib/modules/$K/hwprobe.ko; ls -l /lib/modules/$K/*.omo-off
EOS
# one-shot loader: /etc/init.d/omo-hwprobe -> /etc/rc.d/S99omo-hwprobe

# install the loader and its boot symlink
$ ssh root@192.168.10.1 <<'EOS'
cat > /etc/init.d/omo-hwprobe <<'OMO_LOADER'
#!/bin/sh /etc/rc.common
# One-shot boot loader installed by tools/exp.sh. It self-deletes, re-arms the
# device-side watchdog inside this boot, then insmods the staged module once.
START=99
start() {
	rm -f "/etc/rc.d/S99omo-hwprobe"
	if [ -f /root/recover-exp.sh ]; then
		start-stop-daemon -S -b -m -p /tmp/omo-exp.timer.pid \
			-x /bin/sh -- /root/recover-exp.sh --watch
	fi
	insmod "/lib/modules/$(uname -r)/hwprobe.ko" pollms=100 useirq=1
}
OMO_LOADER
chmod 755 /etc/init.d/omo-hwprobe
ln -sf ../init.d/omo-hwprobe /etc/rc.d/S99omo-hwprobe
sh -n /etc/init.d/omo-hwprobe && echo LOADER_SYNTAX_OK
ls -l /etc/init.d/omo-hwprobe /etc/rc.d/S99omo-hwprobe
EOS

== 3/7 reboot and wait for SSH (bounded poll, no fixed sleeps) ==

# reboot into the takeover boot
$ ssh root@192.168.10.1 <<'EOS'
sync; reboot
EOS
# poll every 3s, deadline 300s, until a FRESH boot answers

# fresh-boot probe
$ ssh root@192.168.10.1 <<'EOS'
u=$(cut -d. -f1 /proc/uptime); [ "$u" -lt 180 ] && echo FRESH_BOOT uptime=$u
EOS

== 4/7 wait for the experiment's completion marker ==
# poll every 3s, deadline 180s

# module loaded + experiment done
$ ssh root@192.168.10.1 <<'EOS'
lsmod | grep -q '^hwprobe ' && echo MODULE_LOADED
dmesg | grep -q 'done (' && echo EXP_DONE
EOS

== 5/7 capture the evidence ==
$ mkdir -p /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>

# capture dmesg
$ ssh root@192.168.10.1 <<'EOS'
dmesg > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/dmesg.txt
EOS

# capture lsmod
$ ssh root@192.168.10.1 <<'EOS'
lsmod > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/lsmod.txt
EOS

# capture interrupts
$ ssh root@192.168.10.1 <<'EOS'
cat /proc/interrupts > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/interrupts.txt
EOS

# capture module log
$ ssh root@192.168.10.1 <<'EOS'
dmesg | grep -i -- 'hwprobe' > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/module-log.txt
EOS

# bundle module-written files
$ ssh root@192.168.10.1 <<'EOS'
cd /; tar -czf /tmp/omo-exp-capture.tar.gz $(for p in /tmp/omo-hwprobe; do [ -e "$p" ] && printf '%s ' "$p"; done) 2>/dev/null
ls -l /tmp/omo-exp-capture.tar.gz
EOS
$ scp -O root@192.168.10.1:/tmp/omo-exp-capture.tar.gz /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/
$ tar -xzf /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>/omo-exp-capture.tar.gz -C /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>
$ ( cd /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS> && tar --exclude=./evidence.tar.gz -czf evidence.tar.gz . )
# evidence dir: /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>   (raw files + evidence.tar.gz)

== 6/7 recover (cancel the watchdog, restore, reboot) ==

# cancel the watchdog and run the recovery by hand
$ ssh root@192.168.10.1 <<'EOS'
touch /tmp/omo-exp.done
/root/recover-exp.sh
EOS
# recovery restores both .omo-off modules, removes the loader,
# the rc.d symlink, the staged module, rmmods it, then reboots

== 7/7 verify health on the recovered boot ==

# health probe
$ ssh root@192.168.10.1 <<'EOS'
K=$(uname -r)
MODS=$(lsmod)
echo "WIFI=$(printf '%s\n' "$MODS" | grep -c '^hi5622v100_wifi ')"
echo "PLAT=$(printf '%s\n' "$MODS" | grep -c '^hi5622v100_plat ')"
echo "WIPHY=$(iw phy 2>/dev/null | grep -c '^Wiphy')"
echo "IFACE=$(iw dev 2>/dev/null | grep -c 'Interface')"
CAL=$(iwpriv vap0 alg get_2g_power_param 2>/dev/null)
echo "CAL_SUCC=$(printf '%s' "$CAL" | grep -c '\[SUCC\]')"
echo "OMO_OFF=$(ls /lib/modules/$K/*.omo-off 2>/dev/null | wc -l)"
echo "STAGED=$(ls /lib/modules/$K/hwprobe.ko 2>/dev/null | wc -l)"
echo "LOADER=$(ls /etc/init.d/omo-hwprobe /etc/rc.d/S99omo-hwprobe 2>/dev/null | wc -l)"
echo "RECOVER=$(ls /root/recover-exp.sh 2>/dev/null | wc -l)"
EOS
# PASS requires WIPHY=2, IFACE=6, CAL_SUCC=1, OMO_OFF=0, STAGED=0, LOADER=0

== summary line printed by a real run ==
EXP RESULT: PASS module=hwprobe params=[pollms=100 useirq=1] evidence=<evidence-dir>/
# on failure instead:
EXP RESULT: FAIL module=hwprobe reason=<step: reason> evidence=<evidence-dir>/
```

---

## 2. `tools/batch.sh` - N hypotheses in ONE boot

```
tools/batch.sh [--dry-run] [--module <ko>] [--params "<base params>"] <list>
```

`<list>` is a text file, one hypothesis per line:

```
<label>|<param>[,<param>...]
```

`#` comments and blank lines are ignored; `<label>` matches `[A-Za-z0-9._-]+` (it names the
evidence sub-directory); `<param>` is a comma-separated set of `key=value` module parameters
appended to the run's base parameters for that hypothesis only. The example list
`tools/batch-hypotheses.example` is four SR-trigger hypotheses:

```
baseline|srctrl=0,intr=0
srctrl1|srctrl=1,intr=0
intr-vendor|srctrl=0,intr=1
intr-or|srctrl=0,intr=2
```

`batch.sh` stages the module **once** with `batch=/tmp/omo-batch/list batchdir=/tmp/omo-batch
<base params>`, copies the list to `/tmp/omo-batch/list`, waits for `/tmp/omo-batch/batch.done`
instead of a dmesg pattern, pulls `/tmp/omo-batch` inside the normal evidence tar, and writes
`<evidence>/report.txt` with one row per hypothesis. `--module` (or `$OMO_MODULE`) selects the
batch-capable module; `--params` sets base parameters shared by every hypothesis.

**Verdict semantics.** `BATCH RESULT: PASS` means the *harness* succeeded: the device returned,
recovery + health passed, and at least one per-hypothesis `result.txt` was collected. It does not
mean a hypothesis worked. The experimental outcome is the per-row `out0_cleared` (and the
`sr1c` / `glue` / `irq` values).

### 2.1 Module-side batch convention (the contract batch.sh relies on)

A batch-capable module MUST implement this convention. It is the only interface between the
runner and the module.

**Parameters**

| param | meaning |
| --- | --- |
| `batch=<path>` | non-empty => batch mode; parse `<path>` and run one hypothesis per active line |
| `batchdir=<dir>` | output root (default `/tmp/omo-batch`); created by the module |

**List format** - identical to the host file: `#` comments and blank lines ignored; each active
line is `<label>|<param>[,<param>...]`. The module applies its own defaults, then the run's base
parameters, then the line's `<param>` list, for that entry only.

**Per-entry output** - for the i-th active line (1-based, file order, `NN = printf %02d i`), the
module writes `<batchdir>/<NN>-<label>/` containing:

| file | content |
| --- | --- |
| `result.txt` | exactly these `key=value` lines, this order: `label=`, `params=`, `out0_cleared=`, `sr1c=`, `glue=`, `irq=` |
| `dmesg.txt` | optional: the entry's raw kernel log lines |

**Observable keys** (the four the batch report folds up):

| key | format | definition |
| --- | --- | --- |
| `out0_cleared` | `y` / `n` | `y` iff the device cleared `out[0]` (H2D pending mask, CA `0x40039010`) at least once during the entry's window |
| `sr1c` | `0x%08x` | SR channel-0 consumer read pointer `+0x1c` at the end of the window |
| `glue` | `0x%08x` | glue status word `oal_pcie_transfer_done` `+0x2ec & 0x3d8` at the end of the window |
| `irq` | decimal | cumulative count for the requested INTx line at the end of the window (from `/proc/interrupts`, e.g. `207`/`209 hisi_pci_intx`) |

**Batch terminator** - after the last entry the module writes `<batchdir>/batch-summary.txt`
(rows `label|out0_cleared|sr1c|glue|irq`, in file order) and then `<batchdir>/batch.done`
(empty sentinel). The runner polls for `batch.done`, so it must be written even if some entries
failed. On an entry error the module writes `result.txt` with `?` for the values it could not
sample and continues; it never reboots between entries.

**State reset between entries (required).** Before each entry the module must restore the base
device state it started from after firmware boot - the same reset it performs at probe start -
so one hypothesis cannot contaminate the next. The base parameters are re-applied, then the
entry's `<param>` overrides.

If `batch-summary.txt` is missing the runner still scans the per-entry `result.txt` files, so a
partial run is reported rather than lost.

### 2.2 Dry run

`bash tools/batch.sh --dry-run tools/batch-hypotheses.example` prints the batch prep, the full
shared exp.sh sequence (with `EXP_EXTRA_STAGE`, `EXP_EXTRA_PULL` and `EXP_DONE_CMD` set), and the
report step, without touching the device:

```
== batch.sh DRY RUN ==
module    : /c/Users/ShibbityShwab/router-openwrt/build/tmp/omo-batch/omo-batch.ko (id omo-batch)
list      : tools/batch-hypotheses.example
base param: []
device    : params=[batch=/tmp/omo-batch/list batchdir=/tmp/omo-batch ]
evidence  : /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/
# NOTHING BELOW IS EXECUTED IN --dry-run.
# validate the list and copy it to a staging path
$ batch.sh: validate tools/batch-hypotheses.example (4 hypotheses)
$ cp tools/batch-hypotheses.example /tmp/omo-batch-list
# the list is staged to the device as /tmp/omo-batch/list by the
# shared exp.sh staging step (EXP_EXTRA_STAGE)
# the harness then runs the shared exp.sh sequence with:
#   EXP_EXTRA_STAGE=/tmp/omo-batch-list:/tmp/omo-batch/list
#   EXP_EXTRA_PULL=/tmp/omo-batch
#   EXP_DONE_CMD=[ -f /tmp/omo-batch/batch.done ]

== 1/7 arm DEVICE-side self-recovery (MANDATORY, before any staging) ==
# cancels via the done flag /tmp/omo-exp.done; reboots on expiry
$ cat > /tmp/recover-exp.sh <<'OMO_RECOVER'
  #!/bin/sh
  # device-side self-recovery for the omo experiment harness (tools/exp.sh).
  #   recover-exp.sh          restore the vendor stack and reboot now
  #   recover-exp.sh --watch  detached watchdog: wait WATCH s, then recover
  #                           unless the done flag has been posted
  # Armed with start-stop-daemon BEFORE anything is staged; cancelled by
  # touching the done flag and running this script by hand.
  MOD=omo-batch.ko            # staged module basename
  MODNAME=omo-batch    # staged module name (no .ko)
  WATCH="${WATCH:-1800}"
  DONE=/tmp/omo-exp.done
  LOG=/tmp/omo-exp.timer.log
  if [ "$1" = "--watch" ]; then
  	echo "watchdog armed $(date -u) pid=$$ wait=${WATCH}s" > "$LOG"
  	sleep "$WATCH"
  	if [ -f "$DONE" ]; then
  		echo "watchdog cancelled: done flag present $(date -u)" >> "$LOG"
  		exit 0
  	fi
  	echo "watchdog FIRING: no done flag, recovering $(date -u)" >> "$LOG"
  fi
  KVER=$(uname -r)
  cd "/lib/modules/$KVER" 2>/dev/null
  [ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
  [ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
  rm -f "/etc/rc.d/S99omo-$MODNAME"
  rm -f "/etc/init.d/omo-$MODNAME"
  rmmod "$MODNAME" 2>/dev/null
  rm -f "/lib/modules/$KVER/$MOD"
  rm -f "/tmp/$MOD"
  rm -f /tmp/omo-exp-capture.tar.gz
  rm -f "$DONE" "$LOG"
  rm -f /root/recover-exp.sh
  sync
  reboot
OMO_RECOVER
$ scp -O /tmp/recover-exp.sh root@192.168.10.1:/root/recover-exp.sh

# chmod + syntax-check + start the watchdog detached
$ ssh root@192.168.10.1 <<'EOS'
chmod 755 /root/recover-exp.sh
sh -n /root/recover-exp.sh && echo RECOVER_SYNTAX_OK
start-stop-daemon -S -b -m -p /tmp/omo-exp.timer.pid -x /bin/sh -- /root/recover-exp.sh --watch
sleep 1
cat /tmp/omo-exp.timer.log
kill -0 $(cat /tmp/omo-exp.timer.pid) 2>/dev/null && echo WATCHDOG_ALIVE
EOS

== 2/7 stage the module (vendor modules hidden as .omo-off) ==
# local md5 of /c/Users/ShibbityShwab/router-openwrt/build/tmp/omo-batch/omo-batch.ko = <not present; md5-checked on a real run>
$ scp -O /c/Users/ShibbityShwab/router-openwrt/build/tmp/omo-batch/omo-batch.ko root@192.168.10.1:/tmp/omo-batch.ko

# push to the boot path and hide the two vendor modules
$ ssh root@192.168.10.1 <<'EOS'
L=; K=$(uname -r); cd /lib/modules/$K || exit 1
R=$(md5sum /tmp/omo-batch.ko | cut -d' ' -f1)
[ "$R" = "$L" ] || { echo MD5_MISMATCH local=$L device=$R; exit 1; }
[ -f hi5622v100_wifi.ko ] && mv -f hi5622v100_wifi.ko hi5622v100_wifi.ko.omo-off
[ -f hi5622v100_plat.ko ] && mv -f hi5622v100_plat.ko hi5622v100_plat.ko.omo-off
cp /tmp/omo-batch.ko /lib/modules/$K/omo-batch.ko
sync
echo STAGED; md5sum /lib/modules/$K/omo-batch.ko; ls -l /lib/modules/$K/*.omo-off
EOS
# extra stage: /tmp/omo-batch-list:/tmp/omo-batch/list

# ensure the remote directory exists
$ ssh root@192.168.10.1 <<'EOS'
mkdir -p "$(dirname '/tmp/omo-batch/list')"
EOS
$ scp -O /tmp/omo-batch-list root@192.168.10.1:/tmp/omo-batch/list
# one-shot loader: /etc/init.d/omo-omo-batch -> /etc/rc.d/S99omo-omo-batch

# install the loader and its boot symlink
$ ssh root@192.168.10.1 <<'EOS'
cat > /etc/init.d/omo-omo-batch <<'OMO_LOADER'
#!/bin/sh /etc/rc.common
# One-shot boot loader installed by tools/exp.sh. It self-deletes, re-arms the
# device-side watchdog inside this boot, then insmods the staged module once.
START=99
start() {
	rm -f "/etc/rc.d/S99omo-omo-batch"
	if [ -f /root/recover-exp.sh ]; then
		start-stop-daemon -S -b -m -p /tmp/omo-exp.timer.pid \
			-x /bin/sh -- /root/recover-exp.sh --watch
	fi
	insmod "/lib/modules/$(uname -r)/omo-batch.ko" batch=/tmp/omo-batch/list batchdir=/tmp/omo-batch 
}
OMO_LOADER
chmod 755 /etc/init.d/omo-omo-batch
ln -sf ../init.d/omo-omo-batch /etc/rc.d/S99omo-omo-batch
sh -n /etc/init.d/omo-omo-batch && echo LOADER_SYNTAX_OK
ls -l /etc/init.d/omo-omo-batch /etc/rc.d/S99omo-omo-batch
EOS

== 3/7 reboot and wait for SSH (bounded poll, no fixed sleeps) ==

# reboot into the takeover boot
$ ssh root@192.168.10.1 <<'EOS'
sync; reboot
EOS
# poll every 3s, deadline 300s, until a FRESH boot answers

# fresh-boot probe
$ ssh root@192.168.10.1 <<'EOS'
u=$(cut -d. -f1 /proc/uptime); [ "$u" -lt 180 ] && echo FRESH_BOOT uptime=$u
EOS

== 4/7 wait for the experiment's completion marker ==
# poll every 3s, deadline 180s

# module loaded + experiment done
$ ssh root@192.168.10.1 <<'EOS'
lsmod | grep -q '^omo-batch ' && echo MODULE_LOADED
[ -f /tmp/omo-batch/batch.done ] && echo EXP_DONE
EOS

== 5/7 capture the evidence ==
$ mkdir -p /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch

# capture dmesg
$ ssh root@192.168.10.1 <<'EOS'
dmesg > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/dmesg.txt
EOS

# capture lsmod
$ ssh root@192.168.10.1 <<'EOS'
lsmod > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/lsmod.txt
EOS

# capture interrupts
$ ssh root@192.168.10.1 <<'EOS'
cat /proc/interrupts > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/interrupts.txt
EOS

# capture module log
$ ssh root@192.168.10.1 <<'EOS'
dmesg | grep -i -- 'omo-batch' > /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/module-log.txt
EOS

# bundle module-written files
$ ssh root@192.168.10.1 <<'EOS'
cd /; tar -czf /tmp/omo-exp-capture.tar.gz $(for p in /tmp/omo-omo-batch /tmp/omo-batch; do [ -e "$p" ] && printf '%s ' "$p"; done) 2>/dev/null
ls -l /tmp/omo-exp-capture.tar.gz
EOS
$ scp -O root@192.168.10.1:/tmp/omo-exp-capture.tar.gz /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/
$ tar -xzf /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/omo-exp-capture.tar.gz -C /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch
$ ( cd /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch && tar --exclude=./evidence.tar.gz -czf evidence.tar.gz . )
# evidence dir: /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch   (raw files + evidence.tar.gz)

== 6/7 recover (cancel the watchdog, restore, reboot) ==

# cancel the watchdog and run the recovery by hand
$ ssh root@192.168.10.1 <<'EOS'
touch /tmp/omo-exp.done
/root/recover-exp.sh
EOS
# recovery restores both .omo-off modules, removes the loader,
# the rc.d symlink, the staged module, rmmods it, then reboots

== 7/7 verify health on the recovered boot ==

# health probe
$ ssh root@192.168.10.1 <<'EOS'
K=$(uname -r)
MODS=$(lsmod)
echo "WIFI=$(printf '%s\n' "$MODS" | grep -c '^hi5622v100_wifi ')"
echo "PLAT=$(printf '%s\n' "$MODS" | grep -c '^hi5622v100_plat ')"
echo "WIPHY=$(iw phy 2>/dev/null | grep -c '^Wiphy')"
echo "IFACE=$(iw dev 2>/dev/null | grep -c 'Interface')"
CAL=$(iwpriv vap0 alg get_2g_power_param 2>/dev/null)
echo "CAL_SUCC=$(printf '%s' "$CAL" | grep -c '\[SUCC\]')"
echo "OMO_OFF=$(ls /lib/modules/$K/*.omo-off 2>/dev/null | wc -l)"
echo "STAGED=$(ls /lib/modules/$K/omo-batch.ko 2>/dev/null | wc -l)"
echo "LOADER=$(ls /etc/init.d/omo-omo-batch /etc/rc.d/S99omo-omo-batch 2>/dev/null | wc -l)"
echo "RECOVER=$(ls /root/recover-exp.sh 2>/dev/null | wc -l)"
EOS
# PASS requires WIPHY=2, IFACE=6, CAL_SUCC=1, OMO_OFF=0, STAGED=0, LOADER=0

== summary line printed by a real run ==
BATCH RESULT: PASS module=omo-batch params=[batch=/tmp/omo-batch/list batchdir=/tmp/omo-batch ] evidence=<evidence-dir>/
# on failure instead:
BATCH RESULT: FAIL module=omo-batch reason=<step: reason> evidence=<evidence-dir>/

== report: fold per-hypothesis observables into report.txt ==
# parse /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/tmp/omo-batch/batch-summary.txt (or each */result.txt)
# columns: label  out0_cleared  sr1c  glue  irq
$ write /c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/report.txt
$ BATCH RESULT: PASS|FAIL hypotheses=4 out0_cleared_yes=<n> evidence=/c/Users/ShibbityShwab/router-openwrt/build/register-dumps/exp/<TS>-batch/
```

---

## 3. Exact verification commands used for this phase

Run from `router-openwrt/` (only the first four are script checks; the router is not contacted):

```
bash -n tools/exp.sh                      # syntax
bash -n tools/batch.sh                    # syntax
test -x tools/exp.sh && test -x tools/batch.sh
bash tools/exp.sh --dry-run build/tmp/hwprobe-art/hwprobe.ko pollms=100 useirq=1
bash tools/batch.sh --dry-run tools/batch-hypotheses.example
```

Both dry runs printed every command in order and made no SSH/SCP call. The outputs are pasted in
sections 1.3 and 2.2 above.

## 4. Scope / notes

- Only `tools/exp.sh`, `tools/batch.sh`, `tools/batch-hypotheses.example` and this doc were added.
  The router was not contacted (`192.168.10.1` untouched), no module was renamed or staged, and no
  other doc was edited.
- The harness reuses the existing `.sshwrap/rsh.sh` / `.sshwrap/rscp.sh` wrappers.
- The one-shot loader and recovery script use the exact device idioms already proven in
  `lab/srt/omo-srt` and `lab/srt/recover-srt.sh` (phase 21).
