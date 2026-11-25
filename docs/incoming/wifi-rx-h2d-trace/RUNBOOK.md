# RUNBOOK - the attended stock-image H2D trace (RX-1/RX-2)

**Owner of the fire:** the Lead. This lane is host-only and did not touch the device. The procedure
below is written so the Lead can execute it step by step with a human at the bench and the recovery
door armed. Nothing in it writes device registers or flash; the only writes are files on the stock
overlay (the aid + sampler + its log) and they are removed in T7.

**Hard rules carried from the project (BENCH-FACTS/HAZARDS, not repeated in full):** UART read-only;
bench link PASS; recovery card READY; never `rmmod` the vendor modules (reboot instead); no writes to
CA `0x400392f0`; never read RC misc `0x10161000`; stock pair A is the control image and is never
flashed; one module-load per boot; a failed rung is analysed offline, never retried in the same boot.

Artifacts used here: `tools/omo_h2dsampler` (device binary), `omo-h2dtrace` (init aid),
`tools/decode_h2d.py` + `tools/ours_h2d_set.txt` (host decode), and the existing project tools
`tools/bench-link.sh`, `tools/recovery-card.sh`, `tools/uart-capture.sh`, `tools/telnet-exec.py`,
`tools/box-provision.sh`.

---

## T0. Host preflight (no device action) ~10 min

```sh
cd /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
tools/session-brief.sh                      # expect: bench link + recovery + branch state
tools/bench-link.sh                         # must PASS (router eth2 <-> enp195s0f3u1; 192.168.1.88 + 192.168.10.44)
tools/recovery-card.sh                      # must say READY; tftp/recovery.bin = tested 2.5.24 image
git -C opensource rev-parse --short HEAD    # record the lane commit in the evidence
mkdir -p build/wifi-rx-h2d-trace/$(date -u +%Y%m%dT%H%M%SZ)
```

Build the device sampler (host cross-compile; local artifact, never committed):

```sh
opensource/docs/incoming/wifi-rx-h2d-trace/tools/build-sampler.sh
# -> build/wifi-rx-h2d-trace/omo_h2dsampler  (static ARM musl; sha256 printed)
file build/wifi-rx-h2d-trace/omo_h2dsampler && sha256sum build/wifi-rx-h2d-trace/omo_h2dsampler
```

Build the payload tree (the aid + the sampler + the boot hook):

```sh
P=build/wifi-rx-h2d-trace/payload
rm -rf "$P"; mkdir -p "$P/etc/init.d" "$P/etc/rc.d" "$P/usr/bin"
cp build/wifi-rx-h2d-trace/omo_h2dsampler "$P/usr/bin/omo-h2dsampler"
cp opensource/docs/incoming/wifi-rx-h2d-trace/tools/omo-h2dtrace "$P/etc/init.d/omo-h2dtrace"
chmod 755 "$P/usr/bin/omo-h2dsampler" "$P/etc/init.d/omo-h2dtrace"
ln -sf ../init.d/omo-h2dtrace "$P/etc/rc.d/S08omo-h2dtrace"
find "$P" -ls
```

Then build the encrypted restore container with the existing lane tool (needs the local bdinfo dump;

fingerprint is hostname + bdinfo secret - the secret is gitignored). Follow
`docs/incoming/cudy-backup-root/REPORT.md` §3 for the upload/restore mechanics:

```sh
.provision-venv/bin/python opensource/docs/incoming/cudy-backup-root/craft_backup.py emit \
  --bdinfo dumps/router-20261108/full/mtd6-bdinfo.bin \
  --dir build/wifi-rx-h2d-trace/payload \
  -o build/wifi-rx-h2d-trace/backup-h2dtrace.bin
# expect: a valid container (about 1-2 KB bigger than the base payload)
```

STOP if: link/recovery not PASS, sampler build failed, or the container cannot be produced (the
stock-root channel is then unavailable - T6's fallback or a bench decision is required).

## T1. Switch the box to stock pair A (attended; recovery armed) ~15 min

Start UART capture first (read-only, to an absolute path):

```sh
tools/uart-capture.sh start --label h2d-stock --timeout 3600
```

From our running pair-B image (SSH `root@192.168.1.1`):

```sh
echo a > /sys/devices/platform/boot-selector/next_pair    # read it back; if permission-denied use the fallback
cat /sys/devices/platform/boot-selector/next_pair
sync; reboot
```
Fallback (documented loader failover): plain `reboot` at the same point.

Confirm on the UART that the **stock 5.10.201** banner booted and that the stock LAN comes up
(`192.168.10.1`). Then get a stock root shell through the documented no-broker channel
(BENCH-NEXT-SESSION §5.4/§6, `docs/incoming/cudy-backup-root/`):

```sh
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'id; uname -r; cat /proc/cmdline'
# expect: uid=0(root) ... 5.10.201 ... ubi.mtd=rootfsa
```
If telnet is refused: `tools/box-provision.sh --bdinfo dumps/router-20261108/full/mtd6-bdinfo.bin
--stage base --host 192.168.10.1 --telnetd` and re-check (this provisions the root channel only).

## T2. Read-only capability + identity gate (STOP on any mismatch) ~10 min

All commands via the stock shell (telnet-exec). Nothing is written in T2.

```sh
# 1) identity of the stock debug-module copies (for the record; the trace does not need them)
sha256sum /lib/hisilicon/ko/wifi_debug.ko /lib/hisilicon/ko/plat_debug.ko 2>/dev/null
uname -r; cat /etc/hi_version | head

# 2) init ordering: the aid must run before the vendor wifi stack loads
ls -l /etc/rc.d/ | grep -iE 'luofu|S0[0-9]|S1[0-9]' | head -20
cat /etc/rc.d/luofu | head -40          # the vendor module-load script (read-only)

# 3) tracefs + kprobes (method B availability)
mount | grep -E 'debug|trace'
cat /sys/kernel/debug/tracing/available_tracers
grep -c hcc_msg_tx /proc/kallsyms; cat /proc/sys/kernel/kptr_restrict

# 4) /dev/mem host-RAM read (method A availability; READ-ONLY)
#    probe only a HOST RAM word here - do NOT touch the ETE/BAR0 window before the
#    vendor PCIe driver has mapped it (an unmapped MMIO read can abort the box):
ls -l /dev/mem
devmem 0x80000000                # host DDR: a plain RAM read, safe
#    The sampler waits for hi5622v100_plat and for SR base != 0 before it reads the
#    ETE window, so the window access is ordered after the vendor init. If /dev/mem
#    RAM reads fail (e.g. STRICT_DEVMEM), use method C (save_hostmem) instead.
```

**STOP conditions:** `/dev/mem` missing or the read faults the box; `/etc/rc.d/luofu` ordering differs
from the documented S08-before-load pattern; no root shell. Record the exact outputs; a STOP is a
finding, not a retry.

## T3. Install the one-shot aid + sampler on stock (persistent files only) ~10 min

Preferred: push the crafted container through the LuCI restore (the cudy-backup-root procedure) -
it extracts our payload tree into `/` (stock overlay) as root. This is the arbitrary-root-file-write
channel; **no flash is involved**. The container's payload is exactly
`etc/init.d/omo-h2dtrace` + `etc/rc.d/S08omo-h2dtrace` + `usr/bin/omo-h2dsampler`.

Alternative if a telnet root shell is already up and the container path is not available: the
sampler binary cannot be pasted over busybox telnet without a base64 applet (2.5.24 busybox has
neither `nc` nor `base64` in the extracted image; `tftp`/devmem/mdev are present), so **the
container path is the supported one** - do not hand-roll a binary transfer.

Verify after the restore (before reboot):

```sh
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'ls -l /etc/init.d/omo-h2dtrace /etc/rc.d/S08omo-h2dtrace /usr/bin/omo-h2dsampler'
# expect: the three entries present, sampler executable
```

## T4. The capture boot (one attended boot; recovery armed) ~10 min

Reboot the stock image (plain reboot; **no RESET**, no flash). The S08 aid starts the sampler before
the vendor stack loads; it logs to `/overlay/h2d-trace.log` and stops after 180 s (defaults).

Watch the UART for the normal stock boot (the Wi-Fi bring-up in the vendor kernel log) and let the
stock AP come up. Then:

```sh
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'ls -l /overlay/h2d-trace.log; head -3 /overlay/h2d-trace.log; tail -3 /overlay/h2d-trace.log; wc -l /overlay/h2d-trace.log'
# PASS (capture exists): the log has R (ring-state) lines and at least a few M (message) lines.
# If the log is absent/empty: check /overlay/h2d-trace.err (the sampler writes its own error log).
```

## T5. Collect the log (read-only transfer) ~10 min

```sh
S=$(date -u +%Y%m%dT%H%M%SZ); mkdir -p build/wifi-rx-h2d-trace/$S
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'cat /overlay/h2d-trace.log' \
  > build/wifi-rx-h2d-trace/$S/h2d-trace.log
python3 tools/telnet-exec.py --host 192.168.10.1 -- 'cat /overlay/h2d-trace.err' \
  > build/wifi-rx-h2d-trace/$S/h2d-trace.err
wc -l build/wifi-rx-h2d-trace/$S/h2d-trace.log
sha256sum build/wifi-rx-h2d-trace/$S/h2d-trace.log
```
If the log is large and telnet truncation is suspected, use the stock `tftp` client to pull it to the
host's TFTP writer directory (`tftp -p -l /overlay/h2d-trace.log -r h2d-trace.log 192.168.10.44`) and
compare the SHA-256 with the on-device value.

## T6. Decode + diff (host; no device) ~15 min

```sh
D=opensource/docs/incoming/wifi-rx-h2d-trace
.provision-venv/bin/python $D/tools/decode_h2d.py \
   --input build/wifi-rx-h2d-trace/$S/h2d-trace.log \
   --ours $D/tools/ours_h2d_set.txt \
   --jsonl build/wifi-rx-h2d-trace/$S/messages.jsonl \
   --diff | tee build/wifi-rx-h2d-trace/$S/DIFF.txt
```

**RX-1 PASS** = `CAPTURE VALID: N messages (>=1), M families, T seconds` with the quarantine rate
reported, and the ordered list saved. **RX-2 input** = the `MISSING-IN-OURS` bucket; take the top
candidate(s) with their exact bytes to the RX-2 analysis (§5/§6 of REPORT.md). If the bucket is
empty, RX-2's "missing host message" hypothesis is refuted - record it and re-plan (the trace was
still the decisive RX-1 result).

Method B (optional complement, run in the same capture boot if kprobes are available): the aid can
also arm the kprobe set listed in REPORT §4.3 and write `trace_pipe` to
`/overlay/h2d-kprobe.log`; decode it with the same tool (`--format kprobe`).

Method C (fallback only, if method A is unavailable): with the stock stack up and
`wifi_debug.ko` loaded once, read the SR base from the ETE window, then
`echo 'save_hostmem 0 0x<base> 4096 /overlay/h2d-bulk.bin' > /sys/hisys/plat/pcie`, fetch the file,
decode the nodes/frames offline with:

    decode_h2d.py --format bulk --dump FILE --dump-base PHY --nodebase SR_BASE --depth 32

(see H2D-FORMATS F2/F3).
Never load `plat_debug.ko` alongside it and never unload either (reboot instead).

## T7. Cleanup + return to pair B (attended) ~15 min

From the stock root shell:

```sh
rm -f /etc/init.d/omo-h2dtrace /etc/rc.d/S08omo-h2dtrace /usr/bin/omo-h2dsampler
rm -f /overlay/h2d-trace.log /overlay/h2d-trace.err /overlay/h2d-trace.stop /overlay/h2d-kprobe.log
ls -l /etc/init.d/omo-h2dtrace 2>/dev/null || echo 'aid removed'
# switch back the documented way (stock shell):
ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
/tmp/fw_setenv bootflag; echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
sync; reboot
```

Confirm our pair-B image on the UART + SSH `192.168.1.1`, re-run `tools/fire-preflight.sh` style
health checks if a fire follows, then `tools/bench-teardown.sh --apply` and `tools/uart-capture.sh
stop`, `tools/recovery-card.sh --stop`.

## Time budget and abort rules

| phase | estimate |
| --- | --- |
| T0 host preflight + build + container | 20 min |
| T1 pair-A switch + root shell | 15 min |
| T2 read-only gate | 10 min |
| T3 install + verify | 10 min |
| T4 capture boot + live check | 10 min |
| T5 collect | 10 min |
| T6 decode/diff | 15 min |
| T7 cleanup + switch back | 15 min |
| **total** | **~1h45 attended** |

* Any golden-state deviation of OUR image pair (before the switch) -> STOP.
* Any exception/page loss on the stock boot -> capture the UART, power-cycle, report (the stock
  control image is known good; a fault there is a finding about the stock image, not a retry item).
* One capture boot per sitting. If the capture is empty, do **not** reboot-and-retry blindly: read
  the sampler error log, fix the cause host-side, and book a new sitting.
