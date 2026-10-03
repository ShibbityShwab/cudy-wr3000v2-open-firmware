# The chip ops table, and the node encoding verified against the vendor's own accessors (phase 25d, 2026-10-03)

Following the SR fill to its caller found no caller - because the fill is not *called*, it is
**registered**. Dumping `.data` where the reference lives exposes the whole chip interface by name.

## The table (`.data`, entries are relocated pointers)

```
0x2904 pcie_main_init        0x2908 pcie_get_res
0x2918 pcie_xfer_data        0x291c pcie_get_status
0x2920 pcie_msg_send         0x2924 pcie_msg_register
0x2928 pcie_reinit           0x292c pcie_close
0x2930 pcie_irq_enable       0x2934 pcie_irq_disable
0x2938 pcie_write            0x293c pcie_read
0x2944 0x4003a000            0x2948 0x40039508          <-- the ETE block and interrupt CAs
0x294c shuangta_ete_sr_node_init_handle   0x2950 shuangta_ete_dr_node_init_handle
0x2954 shuangta_ete_sr_dscr_fill          0x2958 shuangta_ete_dr_dscr_fill
0x295c shuangta_ete_sr_get_nodesize       0x2960 shuangta_ete_sr_get_dscr_addr
0x2964 shuangta_ete_sr_get_dscr_len       0x2968 shuangta_ete_sr_get_dscr_flag
0x296c shuangta_ete_sr_get_dr_dscr_addr   0x2970 shuangta_ete_dr_get_sr_nodesize
0x2974 shuangta_ete_dr_get_dscr_addr      0x2978 shuangta_ete_dr_get_sr_dscr_addr
0x297c shuangta_ete_dr_get_sr_dscr_len    0x2980 shuangta_ete_dr_get_sr_dscr_flag
0x2984 shuangta_ete_dr_set_sr_dscr_flag
```

This is the "shuangta" chip's operation vector: **the PCIe transport** (`pcie_xfer_data`,
`pcie_write`/`pcie_read`, `pcie_msg_send`) and **the ETE descriptor model** - each with an SR (host
to device) and DR (device to host) variant, plus getters for a descriptor's address, length and flag.

Two things fall out of it immediately:

- **`pcie_xfer_data` -> `pcie_tx_request_handle`** (a tail call), so the named transfer chain is
  `pcie_xfer_data` -> `pcie_tx_request_handle` -> ... - readable, and none of it needs the device.
- The **ETE block CA `0x4003a000`** and **interrupt CA `0x40039508`** sit in the same table as
  constants, which is where the port's own ETE/glue addresses came from.

## The node encoding, verified against the vendor's accessors

The node's length and flag are read by two 20-byte functions in the same table:

```
shuangta_ete_sr_get_dscr_len:   ldr r3,[r0]; add r3,r3,r1,lsl #3; ldr r0,[r3,#4]; lsr r0,r0,#0x10
shuangta_ete_sr_get_dscr_flag:  ldr r3,[r0]; add r3,r3,r1,lsl #3; ldr r0,[r3,#4]; ubfx r0,r0,#0,#0xd
```

So a node is `{word0 = buffer address, word1 = (len << 16) | flag}` with the flag in the low 13 bits -
**exactly the layout the port builds** (`(ln << 16) | 0x6d2b`, where `0x6d2b = 0x4000 | 0x2000 |
0x0d2b` accounts for bits 14/13 and the low 13 bits). The port's `omo_sr_post` node construction is now
verified against the vendor's own decoder rather than inferred from one disassembly.

**And the length is an argument.** `shuangta_ete_sr_dscr_fill` takes it in `r3` (masked to 16 bits) and
packs it; the port hardcodes `0x48` for the 72-byte frame and `0x12a` for the alg frame. So the open
question on the body is now precisely two things:

1. **what length the vendor's caller passes** for its first SR frame, and
2. **what bytes it points at.**

Both are behind the ops-table indirection, so the next step is to find who *dispatches through* these
entries - the same route that identified the message handler table earlier - rather than to look for a
direct call, which does not exist.
