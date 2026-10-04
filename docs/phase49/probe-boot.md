# PROBE BOOT: the takeover load path carries a MODIFIED blob without bricking (phase 49, 2026-10-04)

Task 10 of the wifi-forward plan: prove the port's own load path accepts and runs a one-byte-modified
firmware copy, so the scratch boot (task 11) can be trusted to measure the chip rather than the harness.
Evidence `build/register-dumps/exp/20261004-164113/`.

## What was staged

The probe variant is stock `build/tmp/FIRMWARE.bin` with exactly one byte flipped at file offset
`0xe2c97` (`0x00 -> 0x01`), size unchanged at 928920 B, output md5 `2a9a9f1d1340fc63019a69ee8eeca9e0`
(`TASK10-HEADER.txt`). The byte sits at runtime address `0x122c97`, i.e. OUTSIDE the loaded image (the
loaded bound is `0xCC0CC`), so the patch cannot execute: the run measures the load path, not a code path.
The stage is `/lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat`; the stock file is never overwritten.

## The measurement

```
--- fw load   dmesg.txt:693  firmware file /lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat size=928920 bytes
--- fw write  dmesg.txt:694  firmware written to BAR0+0x6f8000 (928920 bytes)
--- readback  dmesg.txt:695  firmware readback diffs=0 match=YES
--- sig       dmesg.txt:717  [sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE
--- done      dmesg.txt:1064 init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded
--- md5 @cap  capture-cmd.txt 2a9a9f1d1340fc63019a69ee8eeca9e0  /lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat
```

`build/register-dumps/exp/20261004-164113/TASK10-HEADER.txt` (acceptance block), `dmesg.txt`,
`capture-cmd.txt` (md5 + a window sanity read `0x406b8000 = 0xE59FF018`).

- `dmesg.txt:693` names the `.omo-pat` file - the port loaded OUR copy, not a fallback. Readback `diffs=0`
  (`dmesg.txt:695`) means the patched byte reached the BAR0 window intact.
- `dmesg.txt:717` `[sig] 9/9` is the chip-left-ROM-state signature, exactly as with the stock blob:
  the chip ran the same 9/9 sequence from the modified image.
- `capture-cmd.txt` md5 at capture time equals the built probe hash, so the file on the device was the
  artifact under test.

## Safety: no panic, no new pstore, clean health

`TASK10-HEADER.txt` records: no panic (only the benign `pstore_zone: registered pstore_blk` boot line at
`dmesg.txt:289`), no new pstore record (3 pre-existing records, identical names/sizes/mtimes before and
after), and `health.txt` `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`.

## Cleanup receipt

`TASK10-HEADER.txt` carries the cleanup receipt: `rm -f` the `.omo-pat` (RM_RC=0); the directory listing
shows only `FIRMWARE.bin`, `cfg_device_hisi.ini`, `cfg_hi5622v100_hisi.ini`; a `find` over
`/lib/firmware /root /lib/modules /tmp` for `*omo-pat*` returns no matches; `md5sum` of the stock file
returns `0e530b976d5a20e87358671f1a577695` (unchanged); pstore still 3 records; loader/symlink/staged .ko
all gone (`health.txt` `LOADER=0 STAGED=0 RECOVER=0`).

## What this settles

GATE G3's happy path holds: `[sig] 9/9` with the modified blob, plus health. The load path carries a
modified blob without bricking the takeover boot, so the scratch boot may proceed. The absent-diff set
(no panic, no new pstore) rules out the trivial "the blob silently failed to load" reading: the port
logged its own file name and a zero-diff readback.
