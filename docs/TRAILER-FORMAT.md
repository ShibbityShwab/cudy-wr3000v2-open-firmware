
## 11. Base-DTB totalsize must be 8-byte aligned (measured 2026-11-21)

The overlay-container gate word is a 32-bit BE load at
base_dtb + FDT-totalsize (section 4; fip-slice.py validate_slice).  A DTB whose
totalsize is not 4-byte aligned makes that load unaligned; on this ARMv7 it
data-aborts inside U-Boot's bootfip.  Measured with the gpio-LEDs/keys DTS: dtc
produced a 13,041-byte blob (strings block ended unaligned), bootfip read the
gate at 0x832032f1, aborted (r1=0x83200040), reset the CPU and the loader fell
back to the stock pair.  The image recipe now runs scripts/pad-dtb.py on
$(LUOFU_BASE_DTB) before append-dtb; it pads to 8 bytes (a superset of the FDT
4-byte rule) and folds the padding into the FDT totalsize, so both the
in-uImage DTB and the trailer agree.  With totalsize 13,048 the gate sits at an
aligned address and the image boots.

Build-time guard: Build/luofu-pad-dtb also asserts len(dtb) == totalsize and
totalsize % 8 == 0, so an unaligned blob can never reach a trailer again.
