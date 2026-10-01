# barsnap: 1 MiB of Wi-Fi BAR space the vendor never dumped (phase 11, 2026-10-01)

**Result: a read-only module snapshotted the first 1 MiB of BAR0 and BAR4 on both Wi-Fi
endpoints and wrote four 1 MiB files to `/tmp`. The whole BAR0 snapshot sits *below* the
vendor's register window, so every byte of it is newly visible space: a 4 KiB ARM boot/ROM
page and a 768 KiB firmware image. BAR4's first 1 MiB reads all-`0xff`. `insmod`/`rmmod`
returned 0, the device did not reboot, and the vendor driver stayed loaded throughout.**

## The module

- Source: `lab/barsnap/barsnap.c` + `lab/barsnap/Makefile` (this repo).
- For each endpoint `0000:00:00.0` and `0001:00:00.0` it:
  1. fetches the endpoint by exact BDF with `pci_get_domain_bus_and_slot(domain, 0, PCI_DEVFN(0,0))`,
     an accessor implemented **inside the vendor kernel** so it uses the vendor's own
     `struct pci_dev` layout (the `resource[]` ABI mismatch documented in `hwprobe.md`);
  2. reads BAR0 and BAR4 (low + high dword) from PCI config space with
     `pci_read_config_dword()` - **read-only**;
  3. `ioremap()`s the first 1 MiB (`SZ_1M`) of each BAR - **no `pci_request_region`, no
     `pci_enable_device`, no claim, no reset**;
  4. `memcpy_fromio()`s the 1 MiB into a vmalloc buffer and writes it with
     `filp_open()` + `kernel_write()` to `/tmp/barsnap_ep<ep>_bar<bar>.bin`, exactly 1 MiB;
  5. counts the 256 4 KiB pages that contain at least one byte that is neither `0x00`
     nor `0xff`, and `pr_info`s the count.
- On unload it `iounmap`s every window and drops both device references.
- Nothing is written to the BAR or to config space at any point.

Symbols used were confirmed present in the vendor kernel's `/proc/kallsyms` before loading:
`vzalloc`, `filp_open`, `kernel_write`, `pci_get_domain_bus_and_slot`, `pci_get_device`.

## CI

- Workflow: `.github/workflows/build-load-test-module.yml` (extended with a `build barsnap
  module` step and a `barsnap-ko` artifact).
- CI run (green): **`36834382334`** -
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36834382334
  - commit `36e643d` "lab: barsnap snapshots 1 MiB of each Wi-Fi BAR read-only", job `build`
    finished in 1m3s, `success`.
  - artifact `barsnap-ko` = `barsnap.ko`, 6852 bytes, md5 `c5229f0bcad15690f9d3614b1f879e56`.
  - fingerprint verified in CI: `vermagic=5.10.201 SMP mod_unload ARMv7` (the vendor
    kernel's required fingerprint).

## On-device evidence, verbatim

The CI artifact was `scp`'d to `/tmp/barsnap.ko` (device md5 `c5229f0bcad15690f9d3614b1f879e56`
= local artifact), then:

    insmod-rc=0
    rmmod-rc=0
    barsnap                16384  0

    [39646.598810] omo-barsnap: probing 0000:00:00.0
    [39646.642937] omo-barsnap: ep0 BAR0 base=0x40000000 cfg=0x40000004: 193/256 4K pages carry data (non-zero, non-0xff), first page 0
    [39646.657515] omo-barsnap: wrote /tmp/barsnap_ep0_bar0.bin (1048576 bytes)
    [39646.711151] omo-barsnap: ep0 BAR4 base=0x41000000 cfg=0x41000004: 0/256 4K pages carry data (non-zero, non-0xff), first page -1
    [39646.725524] omo-barsnap: wrote /tmp/barsnap_ep0_bar4.bin (1048576 bytes)
    [39646.732653] omo-barsnap: probing 0001:00:00.0
    [39646.776727] omo-barsnap: ep1 BAR0 base=0x58000000 cfg=0x58000004: 193/256 4K pages carry data (non-zero, non-0xff), first page 0
    [39646.791144] omo-barsnap: wrote /tmp/barsnap_ep1_bar0.bin (1048576 bytes)
    [39646.844522] omo-barsnap: ep1 BAR4 base=0x59000000 cfg=0x59000004: 0/256 4K pages carry data (non-zero, non-0xff), first page -1
    [39646.859117] omo-barsnap: wrote /tmp/barsnap_ep1_bar4.bin (1048576 bytes)
    [39646.956002] omo-barsnap: unmapped all BAR windows, devices released

The BAR0 bases read from config space (`0x40000000`/`0x58000000`) and BAR4
(`0x41000000`/`0x59000000`) match `lspci`/sysfs exactly. Device uptime was 10:58 before
the run and 11:02 after and the vendor stack (`hi5622v100_wifi`/`hi5622v100_plat`) was still
loaded - no reboot, no vendor module touched.

## The four dumps (pulled to `build/register-dumps/`)

| file | size | md5 |
| --- | --- | --- |
| `barsnap_ep0_bar0.bin` | 1048576 | `5e0e11ec08ec2b738ca8042640f18cd8` |
| `barsnap_ep0_bar4.bin` | 1048576 | `2fdd6851b32ae931637d4845c037b550` |
| `barsnap_ep1_bar0.bin` | 1048576 | `5e0e11ec08ec2b738ca8042640f18cd8` |
| `barsnap_ep1_bar4.bin` | 1048576 | `2fdd6851b32ae931637d4845c037b550` |

Remote and local md5 agree. **The two endpoints are byte-identical in both BARs** (same md5
per BAR): the low 1 MiB of BAR0 is the same on radio 0 and radio 1, consistent with both
radios exposing the same firmware/ROM image.

## Analysis (local, offline)

### BAR0 - ep0 base `0x40000000`, ep1 base `0x58000000`

- Bytes: `0x00` = 41108, `0xff` = 251640, other = 755828.
- **Pages with data: 193/256** (matches the module's own count).
- Per-page map (pages 0-255): page 0 = data; pages 1-3 = all `0x00`; pages 4-63 = all
  `0xff`; pages 64-255 = data.
- Ranges that hold data (BAR offset, 1 MiB snapshot):
  1. `BAR+0x00000..0x00fff` (page 0). Real data occupies `BAR+0x00000..0x00818`; the rest of
     the page is `0x00`.
     First 16 bytes: `18 F0 9F E5 18 F0 9F E5 18 F0 9F E5 18 F0 9F E5`
     (= `0xE59FF018` repeated, an ARM `ldr pc, [pc, #24]` - the chip's firmware boot/ROM
     vector; this is the "offset 0 is the firmware ROM, not registers" fact from
     `HAZARDS.md`).
  2. `BAR+0x40000..0xfffff` (192 pages = 768 KiB, i.e. the whole remainder of the snapshot).
     First 16 bytes: `71 69 04 00 2D 74 0C 00 00 00 00 00 00 00 00 00`
     The 8-byte header `71 69 04 00 2D 74 0C 00` is byte-for-byte the header of
     `/lib/firmware/hi_wifi/FIRMWARE.bin` recorded in `DRIVER-BLACKBOX.md`. So BAR0 from
     `0x40000` exposes the firmware image; that file is 928,920 bytes, so the 768 KiB captured
     here is a prefix of it (it runs past the 1 MiB snapshot limit).
- Non-data regions inside the snapshot: `BAR+0x01000..0x03fff` reads `0x00` (3 pages) and
  `BAR+0x04000..0x3ffff` reads `0xff` (60 pages - decoded by the BAR to nothing).

### BAR4 - ep0 base `0x41000000`, ep1 base `0x59000000`

- Bytes: `0xff` = 1048576, `0x00` = 0, other = 0.
- **Pages with data: 0/256.** The entire first 1 MiB of BAR4 reads `0xff` (no decoding
  target in this window), so there is nothing to report or compare.

## What is newly visible (the point of the exercise)

The vendor dump's addresses are chip addresses (CA) and reach BAR0 through
`BAR0_offset(CA) = 0x3b8000 + (CA - 0x40000000)`:

- vendor dump: CA `0x40000000..0x4011ffff` (highest address actually present in
  `dumps/reg_all.txt` is `0x4011480c`) -> **BAR0 offsets `0x3b8000..0x4d7fff`**.
- barsnap snapshot: **BAR0 offsets `0x0..0x100000`** -> CA `0x3FC48000..0x3FD48000`
  (`CA = 0x40000000 + (offset - 0x3b8000)`).

`0x100000` (snapshot end) is below `0x3b8000` (vendor window start), and there is a
`0x2b8000` (2.72 MiB) gap between them. **Therefore the snapshot and the vendor register
dump do not overlap at all: 100% of the 193 data pages are outside the vendor dump, i.e.
newly visible chip space.**

Newly visible ranges, with the first 16 bytes of each (present identically on both
endpoints):

| # | BAR offset | config address (CA) | content | first 16 bytes (hex) |
| --- | --- | --- | --- | --- |
| 1 | `BAR+0x00000..0x00fff` | `0x3FC48000..0x3FC48FFF` | ARM boot/ROM vector page | `18F09FE518F09FE518F09FE518F09FE5` |
| 2 | `BAR+0x40000..0xFFFFF` | `0x3FC88000..0x3FD47FFF` | firmware image (768 KiB, header matches `FIRMWARE.bin`) | `716904002D740C000000000000000000` |

BAR4 contributes nothing newly visible: its whole first 1 MiB reads `0xff`.

## Overlap comparison against `dumps/reg_all.txt`

The comparison is empty by construction, and that is itself the result: the snapshot's
highest CA (`0x3FD47FFF`) is below `reg_all.txt`'s lowest address (`0x40000000`), so there
is **no address at which both the snapshot and the vendor dump have a value**. A `grep` of
`reg_all.txt` for any address in `0x3FC48000..0x3FD48000` returns nothing.

The known mapping itself is already validated on this device (see `phase11/hwprobe.md`):
`BAR0+0x3b8000` reads `0x101 0x110 0x2`, byte-exact against `reg_all.txt` at CA
`0x40000000/0x40000004/0x40000008`. That anchor lies at BAR0 offset `0x3b8000`, which is
outside this task's "first 1 MiB" snapshot - so the mandated snapshot cannot reach it, and
to compare register *values* one would need a second snapshot starting at `BAR+0x3b8000`
(CA `0x40000000`). That is a natural follow-up, outside the scope specified here.

## Limits, stated plainly

- **First 1 MiB only.** BAR0 offsets `0x100000..0x3b8000` (2.72 MiB, including the vendor's
  window) were not read; BAR4 offsets `0x100000..0x7fffff` were not read.
- **Read-only, no claim.** No `pci_request_region`, no `pci_enable_device`, no config-space
  writes, no reset. The vendor driver owned both devices throughout; no vendor module was
  loaded or unloaded by us.
- **Two endpoints, four 1 MiB files.** `0x00`/`0xff` pages are reported as non-data; a `0xff`
  read means the BAR decoded no target there.
- **BAR4 reads all-`0xff` across its first 1 MiB**, so the 24 MiB of per-radio MMIO is still
  mostly unexplored.

## Reproduce

    # build + CI
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download <run-id> -n barsnap-ko -D /tmp/barsnap-ko

    # device (read-only apart from insmod/rmmod of our own module)
    scp -O /tmp/barsnap-ko/barsnap.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/barsnap.ko; dmesg | grep omo-barsnap; \
                           ls -la /tmp/barsnap_ep*.bin; rmmod barsnap'
    # pull the four files
    scp -O root@192.168.10.1:'/tmp/barsnap_ep*.bin' build/register-dumps/
