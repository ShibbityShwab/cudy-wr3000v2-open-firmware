# The H2D blocker: what the record already proves (phase 23p, 2026-10-02)

Before writing more H2D code, this is what the prior phases already established - so the next
experiment does not repeat a proven negative.

## The host send path is correct and was proven live

`docs/phase20/tx-path.md` + `docs/phase20/fw-accept.md`:

- the endpoint's **SR engine consumed all 32 posted descriptors** - the host->device path works;
- `pcie_msg_send(chip,3)` writes `0x08` (bit 3) to `out[0]` (CA `0x40039010`) and the readback
  **matched** - the send works host-side;
- **the firmware never cleared `out[0]` and never answered.**

## The device side is also already understood

The firmware image itself was analysed (`docs/phase20/fw-accept.md`):

- the firmware **builds its own message context** (maps the six mailbox CAs, zeroes `out[0]`/`out[1]`);
- it **contains a correct `pcie_msg_handle` equivalent at file `0x818a8`** which reads and clears
  `out[0]`, acks `0x400392f0`, re-arms the doorbell `0x400392d4` with `8`, and dispatches the lowest
  set bit;
- **that routine is never invoked in a takeover** - so `out[0]` stays `0x08`.

Three candidate gates were then tested and all **failed to matter**:

| candidate | result |
| --- | --- |
| per-channel ETE control (`+0x00`, `+0x48`) | set by the firmware itself after release; writing it changes nothing |
| firmware message-service enables `0x40101410`/`0x40101430` | set by the firmware itself after release; writing them changes nothing |
| `out[5]` (`0x400392f0`) | it is the firmware's live ack/handshake word; **writing it hangs the chip** (and is forbidden by this project's rules) |

**Named blocker:** the firmware's H2D dispatcher is reached only through the device's PCIe glue ISR /
`pcie_thread`, and a raw takeover never enters it - the trigger (`out[2]`) is consumed by the endpoint
hardware (`readback = 0`) yet the firmware handler does not run.

## A "measured difference" that is not one - corrected here

`fw-accept.md` recorded `0x400392e8` as vendor `0x20` vs takeover `0x3ff`. Read live this session on
the **vendor** boot, that register reads **`0x00000000`** - so `0x20` was a moment-in-time capture, not
a constant, and the "only measured difference" framing in that report should not be used to build a
hypothesis on. `wifidrv1` reads the register before writing it (and logged `pre=0x00000000`), so its
behaviour already matches the vendor's state.

## What this means for the next experiment

The productive paths are exactly the two phase 22 named, and neither is another register poke:

1. **Reproduce the vendor's full runtime binding** - `pcie_msg_init` / `pcie_ete_init` / `hcc_init` and
   the glue-ISR-to-firmware-dispatcher route. `lab/rtmsg` is the closest existing attempt (message
   context + ETE rings + IRQ) and `lab/msghalf` the host half; neither stands up the ISR binding that
   routes the glue status to the firmware dispatcher.
2. **Device-side trace** (JTAG/ROM-monitor or a firmware-side probe) of what gates `0x818a8`.

`wifidrv1` now provides pieces both would use: verified viewport programming, verified ring
programming (3 SR + 4 DR with readbacks), the release write, and a correct read/decode of the message
window - including the corrected log labels. Those are the preconditions; the gate itself remains
device-side.
