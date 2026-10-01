# hwprobe: our module touches the Wi-Fi chip's registers (phase 11, 2026-10-01)

**Result: a module built from public source with our own toolchain loaded on the vendor kernel and
read the Wi-Fi chip's register window at `BAR0+0x3b8000` through `ioremap`, returning the expected
`0x101 0x110 0x2`. It was then unloaded cleanly. Nothing on the device was disturbed.**

This is the first module of ours that touches hardware rather than a log buffer. It is strictly
read-only.

## Setup

- Module source: `lab/hwprobe/hwprobe.c` (+ `lab/hwprobe/Makefile`) in this repo.
- CI workflow: `.github/workflows/build-load-test-module.yml` - the same job now builds a second
  external module, `hwprobe.ko`, using the established phase-11 recipe (vanilla `linux-5.10.201`,
  ARM `multi_v7_defconfig`, `p2v8` stripped from the vermagic header).
- CI run (green): **`36830650736`** - https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36830650736
  - commit `e2ef0f8`, artifact `hwprobe-ko`, module fingerprint verified in CI:
    `vermagic=5.10.201 SMP mod_unload ARMv7`

## What the module does

On load it:

1. `pci_get_device(0x59e7, 0x0005, NULL)` - finds the endpoint. It does **not** claim, enable,
   disable or reset it; the vendor driver keeps ownership.
2. Reads `pci_resource_start(dev, 0)` / `pci_resource_len(dev, 0)` and, as an independent source,
   the raw BAR0 register via `pci_read_config_dword` (a **read**).
3. `ioremap(base + 0x3b8000, 16)` - no `pci_request_region`.
4. `ioread32` of the three words at +0/+4/+8 and `pr_info`s them.

On unload it `iounmap`s the window and `pci_dev_put`s the device reference.

No writes to the BAR, no config-space writes, no `pci_enable_device`, no `device_state` changes.

## The evidence, verbatim from the router

The module was built in CI, `scp`'d to `/tmp/hwprobe.ko` (md5 matches the local artifact
`b5b61ee40273a91a131ef40ca4ffbe0b`), then:

    --- insmod ---
    insmod-rc=0
    [37372.367218] omo-hwprobe: found 59e7:0005 BAR0 resource start=0x0 len=0x0 cfgreg=0x40000004 base=0x40000000
    [37372.376884] omo-hwprobe: BAR0+0x3b8000 = 0x101 0x110 0x2
    --- lsmod ---
    hwprobe                16384  0
    --- rmmod ---
    rmmod-rc=0
    [37372.396590] omo-hwprobe: unmapped BAR window, device released

The three words are exactly the expected `0x101`, `0x110`, `0x2`, read from host address
`0x403b8000` (= BAR0 `0x40000000` + `0x3b8000`). The device was not otherwise disturbed.

## The one real finding: `pci_resource_start()` is unreliable across the ABI boundary

The first build of the module (CI run `36830474854`, artifact `hwprobe-ko`) loaded cleanly but read
the **wrong** values:

    [37256.736173] omo-hwprobe: found 59e7:0005 BAR0 start=0x0 len=0x0
    [37256.742155] omo-hwprobe: BAR0+0x3b8000 = 0x0 0x79016000 0x52039
    [37256.762652] omo-hwprobe: unmapped BAR window, device released

`pci_resource_start(dev, 0)` and `pci_resource_len(dev, 0)` returned `0`. Sysfs on the same device
reports BAR0 as `0x40000000` (len `0x1000000`), so the kernel's own `pci_dev` does hold the resource:
our module read the wrong offset because `struct pci_dev` is laid out for the **vanilla** build's
config and the vendor kernel's layout differs. `ioremap(0 + 0x3b8000)` then mapped unrelated low
memory and returned junk (with first word `0x0`, matching a bare `devmem 0x3b8000`).

The fix reads BAR0 from PCI config space instead - `pci_read_config_dword(dev, PCI_BASE_ADDRESS_0)`,
a read-only accessor implemented inside the vendor kernel that therefore uses the vendor's own
offsets. Both values are printed now (`resource start=0x0 len=0x0 cfgreg=0x40000004 base=0x40000000`),
so the mismatch stays visible. Config-space reads are within the read-only constraint; nothing was
written.

## Both endpoints

`59e7:0005` appears twice. Live `resource` values from sysfs, and direct `devmem` reads:

    0000:00:00.0  BAR0 = 0x40000000  -> 0x403b8000: 0x101 0x110 0x2
    0001:00:00.0  BAR0 = 0x58000000  -> 0x583b8000: 0x101 0x110 0x2

`pci_get_device` returns the first matching endpoint, i.e. `0000:00:00.0` (base `0x40000000`), which
is what the module reported. Both radios expose identical words at the same offset; this module only
probes the first.

## What this proves

1. Code we build from public source can `ioremap` and read the Wi-Fi chip's registers while the
   vendor driver is live, without claiming or resetting the device.
2. The `ioremap`/PCI accessor symbols import and link against the vendor kernel - this is a step
   past `printk` only.
3. Load and unload are clean and reversible.

## Limits, stated plainly

- **Struct-layout ABI risk is real and now demonstrated.** `struct pci_dev` offsets differ between
  our vanilla build and the vendor kernel. Any module that dereferences kernel structs (not just
  calls exported functions) needs per-field validation; this module avoids the issue by using config
  space. This is the main obstacle for anything deeper than MMIO.
- **Read-only, single window.** Only 16 bytes at `BAR0+0x3b8000..0x3b800f` are touched, and only once.
  No writes, no interrupts, no DMA, no firmware loading, no `net_device`/`cfg80211` integration.
- **No vendor module was unloaded or modified.** The vendor driver was live throughout.
- **One endpoint.** The second `59e7:0005` (domain 1) is not probed by this module.
- The stale 37256 lines above are from the pre-fix module and remain in the ring buffer; they are
  labelled to avoid confusion.

## Reproduce

    # build + CI (pushes, then watch)
    gh run list --workflow=build-load-test-module.yml --limit 1
    gh run download <run-id> -n hwprobe-ko -D /tmp/hwprobe-ko

    # device (read-only apart from insmod/rmmod of our module)
    scp /tmp/hwprobe-ko/hwprobe.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/hwprobe.ko; dmesg | grep omo-hwprobe; rmmod hwprobe'
