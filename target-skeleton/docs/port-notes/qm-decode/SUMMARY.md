# incoming/qm-decode - round 4o: mirror / copy-to-CPU experiment (patch 054)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 20).  Prior: `032`...`053`.
This package delivers **patch 054**: the W17 port-mirror / copy-to-CPU experiment for the last known
layer after the first real traffic.  No device access.

## 1. First real traffic (image e924f8a6)

With accepterr, the host's ARP request completed with `RXERR ACCEPTED status=37 port=11`, was
delivered to eth2, the kernel replied, and **the host sniff saw the box's ARP reply on the wire**
(src d4:0d:ab:64:1c:73) - the full wire->PIE RX->CPU->kernel->PIE TX->wire loop works for punted
frames.  But the host's subsequent ICMP unicast to the box MAC produces **zero** RX completions:
normal unicast is not punted/forwarded to the CPU, because the vendor's CFE L2/VLAN/CPU-port
forwarding state is absent (those modules cannot load on 6.18).  That is the last known layer.

## 2. Experiment: recovered port mirror

Decoded `hi_port_set_mirror@0xc10` (hi_lsw_pfe) - the vendor's own mechanism to get a copy of
traffic to a port:

| write | value |
| --- | --- |
| OPP+0xd8 bits15:0 | ingress source-port mask (we use BIT(11) = eth2) |
| OPP+0xd8 bits31:16 | second source list (left as-is) |
| OPP+0xdc bits31:16 | destination field = (old & ~0x7f0) | ((dst & 0xf) << 4) | 0x600 |
| OPP+0xc0 | |= BIT(6) (enable) |
| OPP+0xb4 | write BIT(0), then clear it (latch pulse) |

## 3. Patch 054 (W17) = `luofu_switch.mirror=1@BT@ (+ `mirror_dst@BT@ / `mirror_sweep@BT@)

- Programs the eth2/port-11 ingress mirror to `luofu_switch.mirror_dst@BT@ (default 4), or sweeps
  `4,5,6,7,8,40,41,42,43@BT@ at ~2 s intervals with `luofu_switch.mirror_sweep=1@BT@ so one attended boot
  identifies the port the PIE/CPU side receives mirrored copies on.
- Snapshots OPP 0xb4/0xc0/0xd8/0xdc (two stable reads), programs, read-back verifies the mask/dest/
  enable fields, restores the full snapshot on mismatch; off by default; restore also on remove.
- Mirror semantics are [I] (decoded shape, field meaning partly [U]) - attended experiment only.

## 4. How to run the experiment

Boot the full W3-W16 chain plus:
```
luofu_pie.accepterr=1 luofu_pie.rxdiag=1
luofu_switch.mirror=1 luofu_switch.mirror_sweep=1
```
then run the host ping during the ~18 s sweep.  Expected: `W17_MIRROR: eth2/port 11 ingress mirror ->
dst N ...@BT@ every 2 s, and - if destination N is the CPU/PIE port - PIE RX completions for the host's
ICMP unicast (RXDIAG lines) that stop appearing after the sweep moves on.  The successful N is the
CPU port number; a follow-up can then program the static L2/VLAN/CPU-port forwarding state (or keep
the mirror as the data-path workaround).  If no candidate produces completions, mirroring is not the
punt path and the next target is the IFC/L2-to-CPU action (the FDB/rootif state).

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/054-net-lsw-mirror-cputest-w17.patch` | W17 recovered port-mirror experiment | `e306a155c0fcc2eb6d4ef35ff619cee18d72bb44217dcffe749bb287d201b5d1` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | docs (pending stack) | see handover |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- All patches apply (series ~55/55, 0 rejects) and the driver compiles clean under WERROR; checkpatch
  0 errors (2 cosmetic line-length warnings in the new MODULE_PARM_DESC strings, no checks).
- No devicetree/binding/Kconfig change.
