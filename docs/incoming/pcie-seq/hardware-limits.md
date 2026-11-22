# Hardware / scope limits (what the RC driver cannot do)

## 1. Endpoint PERST and power rails

On this board the vendor's "pcie-gpios" DT property is present but EMPTY:
hi_pcie.ko toggles no pin and the link still trains, so the Hi5622 endpoints
are powered and PERST-released by the bootloader by default. Nothing the RC
driver does is needed there. If the link refuses to train even with the
exact vendor RC sequence, the next suspect is the endpoint side (rail/PERST
state, or a board that does wire GPIOs), which the RC driver cannot fix:

  - the vendor's GPIO mechanism (hi_pcie_enable_dev): per endpoint,
    output 0 -> wait reset-duration-ms x delay(0x0CCCCB60) -> output 1 ->
    wait gpio-delay (50) x delay(0x0CCCCB60), con-id "pcie",
    gpios from "pcie-gpios". The pin numbers for this PCB are unknown and
    must come from schematic/GPIO discovery, not from the RC register map.
  - iomux: no pcie iomux property exists in the vendor DT, so the default
    mux state works; if a variant needs a PERST pin muxed, that is a pinctrl
    (hi_kpinctrl / patch 011) concern.

## 2. Inbound iATU windows (endpoint DMA)

The RC inbound windows are programmed by the Wi-Fi plat driver
(hi5622v100_plat.ko) AFTER link-up, through the RC's own config space
offsets 0x900-0x91c (viewport CTRL1 = 0x80000000|idx, CTRL2 = 0x80000000|
(type<<8), base/limit/target at 0x90c-0x91c) plus endpoint-side region
tables written through the endpoint BAR windows and a doorbell write
(1 -> device address 0x4003a200). These are the Wi-Fi DMA map and belong to
the Wi-Fi lane's future driver; they are not required for link training.
The exact region contents are runtime-built (g_region_table) from static
descriptors (g_shuangta_region_types, .data+0x29d8, 15x32 B) - a task for
the wifi lane, not patch 013.

## 3. MSI / radm / linkdown interrupts

Vendor interrupt plumbing: misc+0x04 = 0x00200000 and misc+0x18 =
0x08000000 before init, misc+0x04 = 0 after link-up, misc+0x1c |=
0x02200000 (irq mask), misc+0x2c |= 0x00800000, misc+0x28 &= ~0x00001000,
linkdown IRQ = GIC SPI 69/70. The semantics are inferred (radm/linkdown
related); the vendor never documents the radm->MSI mapping either, so patch
013 stays polling-based and these writes are applied for fidelity only.
hwver==32 variants use different bits (linkdown bit 7, status at
misc+0x200) - not this board.

## 4. The misc-window read prohibition vs the vendor

The vendor driver reads misc+0x1c/0x28/0x2c/0x100/0x104/0x110/0x200 freely
in-kernel. Our standing rule forbids reading the window (a userspace read of
0x10161000 panicked the board once). The two observations are compatible if
only the control offsets (0x00/0x04/0x18) or user-context reads are
dangerous. Nothing in this fix list requires reading misc (link state comes
from DBI DEBUG0/DEBUG1). A careful in-kernel readback test of misc+0x1c,
with the recovery door armed, would settle whether we can switch from
absolute writes to the vendor's RMW - do not attempt it from userspace and
do not read 0x10161000+0x00.

## 5. Live-DTB discrepancy

/tmp/vendor-base.dts says pcie capability = <0x01> and hwver = <0x10> for
both RCs. The live system behaves like capability bit 1 is SET (it prints
"2.0 Device Link Up", which only happens when capability & 2, and it writes
speed = 2, matching the measured LNKCAP 0x734c12 = 5 GT/s). The live hwver
must be 0x10 for both (a hwver-32 RC1 would require a nonexistent "iatu"
reg resource and vendor probe would fail). Re-decompile the DTB of the
running image to confirm capability; if it is 2, the speed-cap and stage-2
retrain writes in the fix list are exactly what the live vendor runs.

## 6. ACP gate

0x10100174 bits 25/26 (RC0 mask 0x02000000 / RC1 mask 0x04000000, value 0).
Bootloader leaves them clear (live read 0x00210321). hi_acp.ko's
hi_acp_hw_set_cache_attr does (v & ~mask) | value - identical to patch 013's
acp-gate handling. The register is in the ACP block, NOT the forbidden misc
window, and may be read freely. Not a link-train step on this board.

## 7. Delay constants

0x20c498 (reset spacing) and 0x0CCCCB60 (link poll interval) are raw values
passed to arm_delay_ops.delay with lpj = 9969664: ~0.2-0.9 ms and ~20-90 ms
respectively (CONFIG_HZ dependent, see vendor-pcie-sequence.md). Exact
vendor timings are not load-bearing; the order and the gate discipline are.
