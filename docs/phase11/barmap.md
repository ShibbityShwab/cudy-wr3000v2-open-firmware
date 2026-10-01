# barmap: the whole 16 MiB BAR0 window, page by page (phase 11, 2026-10-01)

**Result: a read-only module mapped all 16 MiB of BAR0 (and probed BAR4) on both Wi-Fi
endpoints, classified every 4 KiB page, and dumped BAR0 raw. The window is the chip's
on-chip memory: the 835,788-byte firmware image that `FIRMWARE.bin` only prefixes, the
WRAM calibration block at offset `0x1b2f00` (exactly the CPU address from
`phase7/firmware-ram-dump.md`), and the vendor register block at `0x3b8000`. The upper
7.19 MiB reads all-`0xff`. `insmod`/`rmmod` returned 0, the device did not reboot, and no
vendor module was touched.**

`barsnap.md` snapshotted the first 1 MiB and explicitly stopped below the register window.
This module reads the full length, so the register anchor `0x101 0x110 0x2` is inside the
same dump as the firmware image and the calibration tables - the things that were only
implied before are now one contiguous byte record.

## The module

- Source: `lab/barmap/barmap.c` + `lab/barmap/Makefile` (this repo).
- For endpoint `0000:00:00.0` (and `0001:00:00.0` with the same page map, no raw dump) it:
  1. reads the BAR base from PCI config space with `pci_read_config_dword()` - a
     vendor-kernel accessor, so no `struct pci_dev` layout risk (the `resource[]` ABI
     mismatch from `hwprobe.md`);
  2. `ioremap()`s BAR0 in 1 MiB chunks across the full 16 MiB upper bound - **no
     `pci_request_region`, no `pci_enable_device`, no claim, no reset**; BAR4 is bounded at
     8 MiB;
  3. classifies every 4 KiB page (all-`0x00`, all-`0xff`, or data), computes a 128-bit
     hash (four FNV-1a lanes) of each data page and records its first 16 bytes, then writes
     one line per page to `/tmp/barmap_ep<ep>.txt`;
  4. appends the raw BAR0 to `/tmp/barmap_ep0_bar0.bin` in 1 MiB chunks (16 MiB; the file
     is `O_TRUNC`'d first);
  5. reads the register anchor at `BAR0+0x3b8000` and `pr_info`s the three words as an
     on-device cross-check.
- `iounmap` on unload; the anchor windows are unmapped in `module_exit`. Read-only,
  no writes to the BAR or config space.
- BAR4 stops at the first all-`0xff` chunk (it has nothing to show). BAR0 deliberately
  scans all 16 MiB even though chunks 10-16 are all-`0xff`: the raw dump is required to be
  the full length and the register anchor at `0x3b8000` must be reached.

### The first run had a real bug, and it was caught

The first green CI (run `36835042633`, commit `c093ab7`) wrote BAR0's page map to
`/tmp/barmap_ep0.txt` and then the BAR4 probe overwrote the *same* file with its own
(4684-byte) map, so the 149,549-byte BAR0 map was lost. Fix: BAR0 keeps the required
`ep<ep>.txt` name, other BARs get a `_bar4` suffix. Commit `5e77a33`, run `36835217993`.
The notes below are from the fixed run.

## CI

- Workflow: `.github/workflows/build-load-test-module.yml` (added a `build barmap module`
  step and a `barmap-ko` artifact; `barmap.ko` is in the vermagic check).
- CI run (green): **`36835217993`** -
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36835217993
  - commit `5e77a33`, conclusion `success` (1m30s).
  - artifact `barmap-ko` = `barmap.ko`, 10452 bytes, md5 `08d2f7eac0b4bfa7b71e285ee13ac626`,
    fingerprint `vermagic=5.10.201 SMP mod_unload ARMv7`.

## The sysfs pre-check (userspace, before the module)

Read-only `cat` of `/sys/bus/pci/devices/*/resource`:

| endpoint | BAR0 | BAR2 | BAR4 |
| --- | --- | --- | --- |
| `0000:00:00.0` | `0x40000000`, len `0x1000000` (16 MiB) | `0x41800000`, len `0x4000` (16 KiB) | `0x41000000`, len `0x800000` (8 MiB) |
| `0001:00:00.0` | `0x58000000`, len `0x1000000` (16 MiB) | `0x59800000`, len `0x4000` | `0x59000000`, len `0x800000` |

So the 16 MiB / 8 MiB bounds in the module are exactly the real resource lengths; nothing
is read past a BAR.

## On-device evidence, verbatim (fixed run)

The CI artifact was `scp`'d to `/tmp/barmap.ko` (device md5
`08d2f7eac0b4bfa7b71e285ee13ac626` = local artifact), then:

    === BEFORE ===   09:03:15 up 11:09
    === INSMOD ===   insmod-rc=0   elapsed=3s
    [40166.679006] omo-barmap: ep0 BAR0 base=0x40000000 len=0x1000000 -> raw dump /tmp/barmap_ep0_bar0.bin
    [40166.751859] omo-barmap: ep0 BAR0 1/16 MiB @0x00000000: data=193 zero=3 ff=60
    [40166.823530] omo-barmap: ep0 BAR0 2/16 MiB @0x00100000: data=201 zero=55 ff=0
    [40166.884711] omo-barmap: ep0 BAR0 3/16 MiB @0x00200000: data=80 zero=144 ff=32
    [40166.948524] omo-barmap: ep0 BAR0 4/16 MiB @0x00300000: data=75 zero=146 ff=35
    [40167.023468] omo-barmap: ep0 BAR0 5/16 MiB @0x00400000: data=132 zero=94 ff=30
    [40167.086086] omo-barmap: ep0 BAR0 6/16 MiB @0x00500000: data=80 zero=144 ff=32
    [40167.146094] omo-barmap: ep0 BAR0 7/16 MiB @0x00600000: data=51 zero=145 ff=60
    [40167.222821] omo-barmap: ep0 BAR0 8/16 MiB @0x00700000: data=248 zero=8 ff=0
    [40167.285532] omo-barmap: ep0 BAR0 9/16 MiB @0x00800000: data=99 zero=45 ff=112
    [40167.337830] omo-barmap: ep0 BAR0 10/16 MiB @0x00900000: data=0 zero=0 ff=256 (all-0xff)
    ... chunks 11-16 all data=0 zero=0 ff=256 (all-0xff) ...
    [40167.667840] omo-barmap: ep0 BAR0 wrote /tmp/barmap_ep0.txt (149549 bytes)
    [40167.674649] omo-barmap: ep0 BAR0 raw dump complete (16777216 bytes)
    [40167.681161] omo-barmap: ep0 BAR0 totals: data=1159 zero=784 ff=2153 pages
    [40167.692033] omo-barmap: ep0 BAR0+0x3b8000 = 0x101 0x110 0x2
    [40167.740702] omo-barmap: ep0 BAR4 1/8 MiB @0x00000000: data=0 zero=0 ff=256 (all-0xff)
    [40167.748514] omo-barmap: ep0 BAR4 stopped early: chunk @0x0 reads all-0xff
    [40168.704722] omo-barmap: ep1 BAR0 wrote /tmp/barmap_ep1.txt (149549 bytes)
    [40168.711568] omo-barmap: ep1 BAR0 totals: data=1159 zero=784 ff=2153 pages
    [40168.718569] omo-barmap: ep1 BAR0+0x3b8000 = 0x101 0x110 0x2
    === RM MOD ===   rmmod-rc=0
    === AFTER ===    09:03:18 up 11:09

Device uptime `11:07` before the first run and `11:09` after the second - no reboot. The
full dmesg for the run is in `build/register-dumps/barmap_ep0_dmesg.txt`.

## The pulled artifacts

| file | bytes | md5 (device = local) |
| --- | --- | --- |
| `build/register-dumps/barmap_ep0.txt` | 149549 | `0f17cb1eff255d8cf8a85ba6dfbc850d` |
| `build/register-dumps/barmap_ep0_bar0.bin` | 16777216 | `40a1da524539c3392d3c04694ffafb4d` |
| `build/register-dumps/barmap_ep1.txt` | 149549 | `f13823e25c8bee0153e6fa9e702b72b0` |
| `build/register-dumps/barmap_ep0_dmesg.txt` | 4131 | `cda49e3f1335c5ddae1f6c88f60b161a` |

Local sha256 of the two required files: `barmap_ep0.txt` =
`edb01d2d759f66311a61215d77bbe84e0a3aaeeb392504ebe398c5f0bc02b886`,
`barmap_ep0_bar0.bin` = `4ef73c9b46bcd51b3914b25063bb87b76946041232d666e195ce08d8ad9501d7`.

## The region map of the 16 MiB window

Page counts over the whole BAR0: **data 1159, zero 784, ff 2153** (of 4096 4 KiB pages).
`0xff` is what an undecoded BAR read returns; `0x00` is mapped-but-zero memory.

| BAR0 range | size | content |
| --- | --- | --- |
| `0x000000`..`0x000fff` | 4 KiB | **ARM boot/ROM exception-vector page** (`18f09fe518f09fe518f09fe518f09fe5`, `ldr pc,[pc,#24]` x4) |
| `0x001000`..`0x003fff` | 12 KiB | mapped, zero |
| `0x004000`..`0x03ffff` | 240 KiB | undecoded (`0xff`) |
| `0x040000`..`0x10c0cb` | 816 KiB | **firmware image prefix, byte-identical to `FIRMWARE.bin`** (835788 bytes) |
| `0x10c0cc`..`0x11dfff` | 72 KiB | firmware tail beyond the file (data; loader/fixup table - see below) |
| `0x11e000`..`0x125fff` | 32 KiB | mapped, zero |
| `0x126000`..`0x2b7fff` | 1.5 MiB | mapped image/runtime data, incl. the **calibration block at `0x1b2f00`** |
| `0x2b8000`..`0x2d7fff` | 128 KiB | undecoded (`0xff`) |
| `0x2d8000`..`0x3c5fff` | 952 KiB | mapped; contains the **register block `0x3b8000`..`0x3c5fff`** |
| `0x3c6000`..`0x4d7fff` | 1.06 MiB | register-window remainder with `0xff` holes (matches the vendor dump extent CA `0x40000000`..`0x4011ffff` = BAR0 `0x3b8000`..`0x4d7fff`) |
| `0x4d8000`..`0x8cffff` | 3.97 MiB | mapped image/runtime data (contains a **second copy of the firmware image at `0x6f8000`**) |
| `0x8d0000`..`0xffffff` | 7.19 MiB | undecoded (`0xff`) - empty |
| BAR4 `0x000000`..`0x7fffff` | 8 MiB | first chunk all-`0xff`; stopped early, whole window undecoded |

### What the window actually is

- **BAR0 offset X shows the chip's on-chip memory at CPU address X.** BAR0
  `0x0`..`0x40000` is *byte-identical* to the WRAM dump `fw_ram_wram_0x0_0x1bffff.bin`
  over the same range, and BAR0 `0x40000`..`0x10c143` is byte-identical to that WRAM dump
  for 835,908 bytes. The ARM vector page at BAR0 `0x0` and the loaded image at BAR0
  `0x40000` are the same bytes the chip's CPU sees at `0x0` and `0x40000`.
- **WRAM (CPU `0x0`..`0x1bffff`) is visible in the window** at the same offsets - that is
  where the calibration block comes from. **TCM (CPU `0x400000`..`0x417fff`) is not**:
  BAR0 `0x400000`..`0x417fff` differs from `fw_ram_tcm_0x400000_0x417fff.bin` in 84.8% of
  bytes.
- **The register block is overlaid at `0x3b8000`**, and only there: BAR0 `0x3b8000` reads
  CA `0x40000000` byte-exactly (cross-check below). This is the `SHUANGTA_REGION_IO`
  window from `HAZARDS.md`, so the BAR mixes memory at low offsets with the register file
  at `0x3b8000`.

### Aliases / repeats (measured, mechanism not claimed)

The window is not a flat image; identical bytes appear at more than one offset:

- The 835,788-byte firmware image appears **twice**, at BAR0 `0x40000` and `0x6f8000`
  (shift `+0x6b8000`); both match `FIRMWARE.bin` for the same 835,788 bytes.
- The 576-byte calibration block likewise appears twice, at `0x1b2f00` and `0x86af00`
  (same `+0x6b8000`).
- A ~3 MiB physical image repeats at `+0x300000`: data runs match at 99.5-99.9%, and the
  zero/`0xff` runs match exactly (e.g. BAR0 `0x259000`..`0x2b7fff` == `0x559000`..`0x5b7fff`).
  The small mismatches are consistent with live register/runtime words read at different
  instants; the mechanism is left as an observation, not a claim.

## Where the firmware image ends

`FIRMWARE.bin` is 928,920 bytes. The byte-identical prefix in BAR0 is **835,788 bytes
(`0xcc0cc`)**, so

    firmware image (file-identical extent) = BAR0 0x040000 .. 0x10c0cb

`0x1000000` and BAR4's first chunk are the same story. **The file's last ~93 KiB are all
zero, while BAR0 `0x10c0cc` onward is real data** (`cc010000 2dc9ffff ...`, a run of
signed 32-bit offsets - a loader/fixup or relocation table). So `FIRMWARE.bin` is a
partial/trimmed copy: the device runs a longer image than the file contains. The mapped
firmware data run continues to `0x11dfff` before the 32 KiB zero gap; the loaded image as
a whole continues through the calibration block and to `0x2b7fff`.

## Are the calibration tables visible? Yes.

The 576-byte WRAM calibration block from
`build/register-dumps/fw_ram_calib_0x1b2f00_576_run2.bin` (CPU `0x1b2f00`, phase 7) is
**present byte-for-byte in the BAR0 window at offset `0x1b2f00`**, including the 26-word
2.4 GHz power table at `0x1b2f24`:

- `0x17161605` at `0x1b2f24/0x1b2f28/0x1b2f2c`
- `0x0a0606ff` at `0x1b2f80/0x1b2f84/0x1b2f88`
- the whole 576-byte block matches at `0x1b2f00` and again at `0x86af00`.

This is the static image copy of the tables: BAR0 offset equals the CPU address, and the
image is loaded at `0x40000` in both views, so the table the firmware copies to WRAM
`0x1b2f24` is the one stored at BAR0 `0x1b2f24`. It is **not** in `FIRMWARE.bin` itself
(the phase-7 negative stands); it is in the longer image the BAR exposes.

## Non-empty page count per MiB

"Non-empty" = a 4 KiB page with at least one byte that is neither `0x00` nor `0xff`
(i.e. the `data` class). Counts are out of 256 pages/MiB.

| MiB | data | zero | ff | | MiB | data | zero | ff |
| ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: |
| 0 | 193 | 3 | 60 | | 8 | 99 | 45 | 112 |
| 1 | 201 | 55 | 0 | | 9 | 0 | 0 | 256 |
| 2 | 80 | 144 | 32 | | 10 | 0 | 0 | 256 |
| 3 | 75 | 146 | 35 | | 11 | 0 | 0 | 256 |
| 4 | 132 | 94 | 30 | | 12 | 0 | 0 | 256 |
| 5 | 80 | 144 | 32 | | 13 | 0 | 0 | 256 |
| 6 | 51 | 145 | 60 | | 14 | 0 | 0 | 256 |
| 7 | 248 | 8 | 0 | | 15 | 0 | 0 | 256 |

Total data pages 1159 = **4.53 MiB of non-empty BAR0**; the last 7.19 MiB are untouched
`0xff`.

## Register cross-check

Both endpoints return the mandated anchor, once in the dmesg and again in the raw dump:

    omo-barmap: ep0 BAR0+0x3b8000 = 0x101 0x110 0x2
    omo-barmap: ep1 BAR0+0x3b8000 = 0x101 0x110 0x2

and from `barmap_ep0_bar0.bin`, `u32[0x3b8000/4] = 0x00000101, 0x00000110, 0x00000002`
(page first-16-bytes `01010000100100000200000000000000`). This matches `reg_all.txt` at CA
`0x40000000/04/08` exactly - the anchor sits at BAR0 `0x3b8000`, inside the full dump.

## Both endpoints

`0001:00:00.0` (BAR0 base `0x58000000`) produced the same page counts (data 1159, zero
784, ff 2153) and the same anchor (`0x101 0x110 0x2`). Its page map differs from ep0's in
**only 183 of 4096 pages**, all of them live/runtime pages (the firmware's variable area
and the register file, whose hashes move between reads); the static image is identical
across the two radios.

## Limits, stated plainly

- **Read-only, no claim.** No `pci_request_region`, no `pci_enable_device`, no config-space
  writes, no reset. The vendor driver owned both devices throughout; no vendor module was
  loaded or unloaded by us.
- **BAR2 (16 KiB) and the full BAR4 (8 MiB) were not dumped.** BAR4's first chunk is
  all-`0xff` and the scan stops there; BAR2 was out of scope.
- **`0xff` means undecoded, not "empty file".** The 7.19 MiB `0xff` tail and BAR4 are
  reads the BAR decoded to nothing.
- **Live words exist.** The register file and firmware variables change between reads; the
  page hashes for those pages are snapshots, and the ep0/ep1 hash differences are exactly
  those pages. The raw dump is a single instant.
- **Aliasing is observed, not explained.** The `+0x6b8000` firmware copy and the
  `+0x300000` repetition are measured; the chip's address decode behind them is not
  reverse-engineered here.

## Reproduce

    # build + CI
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download <run-id> -n barmap-ko -D /tmp/barmap-ko

    # device (read-only apart from insmod/rmmod of our own module)
    scp -O /tmp/barmap-ko/barmap.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/barmap.ko; dmesg | grep omo-barmap; \
                           ls -la /tmp/barmap_ep0*; rmmod barmap; uptime'
    scp -O root@192.168.10.1:/tmp/barmap_ep0.txt root@192.168.10.1:/tmp/barmap_ep0_bar0.bin \
           build/register-dumps/

Analysis was done offline against the pulled `barmap_ep0_bar0.bin` (classification, the
`FIRMWARE.bin` prefix, the WRAM/TCM diffs, and the calibration-block search); every number
above is reproducible from that file.
