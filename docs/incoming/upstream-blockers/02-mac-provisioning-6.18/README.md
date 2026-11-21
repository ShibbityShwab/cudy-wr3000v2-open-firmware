# Blocker 3 - MAC addresses without v6.19 nvmem cells

## Problem

The current draft DTS uses `fixed-layout`/`mac-base` nvmem cells
(`nvmem-layout` under the bdinfo partition, `nvmem-cells` on the GEMACs).
Those bindings are v6.19 content; a 6.18 submission cannot rely on them.  The
measured MAC source is unchanged: the plaintext 6-byte factory copy at
`bdinfo+0xde00` (vendor userspace reads it with `mtd_get_mac_binary` /
`bdinfo mac`; LAN = base, WAN = base+1).

## 6.18-compatible alternative (recommended)

- Keep the stock partition map, including the `bdinfo` partition
  (`0x4c0000 0x40000`, `read-only`) - it is a measured fact.
- **Drop** the `nvmem-layout`/`mac-base` block and the four `nvmem-cells`/
  `nvmem-cell-names` pairs from the DTS draft
  (`dts-drop-mac-nvmem-6.18.diff`).
- Provision MACs from userspace, which is the vendor-measured path and is
  already implemented in the target tree:
  `target/linux/hisilicon/base-files/lib/functions/luofu-mac.sh` +
  `base-files/etc/board.d/02_network` (reads bdinfo, validates unicast/global,
  derives WAN = LAN+1 in the low 24 bits, records via
  `ucidef_set_interface_macaddr`; random locally-administered fallback if
  invalid).  It is **host-validated only**; no router run yet.
- The GEMAC driver keeps its existing random-MAC fallback when no `mac-address`
  provider is present (patch 026 already honours `nvmem-cells` when present
  and falls back otherwise); no driver forcing is needed.
- 6.19 follow-up: re-add the nvmem cells when the baseline moves, and re-test
  that userspace and kernel provisioning agree (idempotent), as the current
  target design intends.

## Why not static `mac-address` properties

Per-unit MACs cannot be baked into a board DTS; upstream policy is to get them
from nvmem/bootloader/userspace provisioning, not from a static property.

## Hardware dependency

The DTS change is active: boot-test that the GEMACs still probe (random
fallback) and that the userspace MAC provisioning records the correct LAN/WAN
addresses.  No kernel build is needed for the userspace part beyond the existing
target image.

## Draft artifact

- `dts-drop-mac-nvmem-6.18.diff` - removes only the nvmem wiring; partition
  map and measured MAC facts are unchanged.
