# The vendor's H2D mailbox is boot-only - the gate is bounded (phase 36, 2026-10-04)

Relocation scan of `hi5622v100_plat.ko` and `hi5622v100_wifi.ko` for every call site of the two
H2D send entry points:

```
plat: pcie_msg_send_irq  @0xb794   from pcie_msg_init                       (init)
plat: pcie_msg_send      @0x15144  from pcie_ete_rcv_buff_check             (init)
plat: pcie_msg_send      @0x178f8  from shuangta_ete_sr_dscr_fill           (the SR announce, boot)
plat: pcie_msg_send      @0xaf8    from check_customize_module_exist        (init)
plat: pcie_msg_send      @0x2920   from hwifi_get_ini_chain_customize_param (init)
wifi: none.
```

**The vendor's own driver sends H2D mailbox messages only during init** - the announce, a receive-buffer
check, and two customize-module queries - and never in steady state.  wifi.ko never sends a mailbox
message at all.  The steady-state host<->device protocol rides the SR/DR rings and the D2H mailbox,
both of which the port already exercises (phase 24-27).

## What this does to the gate

Interrupt line 0x4C (the H2D dispatcher's line, phase 32) is asserted only at boot in a vendor boot -
four messages, all before the radios come up.  The phase-35 finding (the dispatcher never runs in a
takeover) therefore bounds the loss to the boot-time mailbox dialogue, and H1 (phase 27) already
showed the full boot dialogue runs via the rings with the mailbox untouched.  The gate does NOT block
the steady-state protocol.

## What remains, honestly

1. The ring-protocol build-out: implement the vendor's host-side steady-state protocol (SR fill
   patterns, D2H servicing, the cfg80211-facing layer) on the port.  This IS the custom-firmware
   objective's main engineering track - long, device-side-free, no new information needed.
2. A device-side trace (JTAG/ROM-monitor) to observe the mailbox forwarding directly, if the boot-time
   mailbox dialogue is ever wanted.
3. The vendor source (set aside by the human's decision).

