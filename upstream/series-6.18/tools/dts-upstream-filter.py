#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Strip release-line-only content from the single-source board DTS.

The board DTS in opensource/target-skeleton/dts/luofu-r116.dts is also the
release-line source, so it carries the 2026-11-22 W23 thermal delta (target
patches 066/067).  That delta is release-line-only: the upstream review form
still carries the original thermal driver/binding (patch 0012), whose binding
documents a 0x100-byte register window with `unevaluatedProperties: false` and
no hwver/trim/threshold properties.  A DTS-0001 built verbatim from the
release-line DTS would therefore fail dtbs_check (and would not be reviewable
without the release-line-only binding update).

This filter is the single definition of the release-line -> upstream DTS delta.
It fails closed: the expected release-line block must occur exactly once and
the filtered text must not contain any of its W23 markers.

Usage:
  tools/dts-upstream-filter.py <target.dts> [<out.dts>]
  tools/dts-upstream-filter.py --check <target.dts>
      exit 0 if the release-line block is present and strips cleanly

Without <out.dts> the filtered DTS is written to stdout.
"""

import sys
from pathlib import Path

# The W23 release-line thermal delta (target patches 066/067) as it appears in
# the single-source board DTS (2026-11-22).
RELEASE_LINE_BLOCK = """	tvsensor: thermal-sensor@14900500 {
		compatible = "hisilicon,luofu-tvsensor";
		/*
		 * The sensor block plus the +0x400 addition-enable word; the
		 * vendor DT's 0x14 is far too short.  If the PMU-PWM node at
		 * 0x14900800 is ever enabled, split this into two resources.
		 */
		reg = <0x14900500 0x408>;
		#thermal-sensor-cells = <0>;
		hwver = <3>;
		trim = <0 0>;
		int-base-offset = <0xc>;
		addition-en;
		efuse-trim-en;
		/* vendor critical trip, bench-verification helper only */
		hisilicon,threshold-mc = <125000>;
		status = "okay";
	};
"""

# The upstream form the kernel review form expects (patch 0012 binding: a
# register window that covers offset 0x24; no W23 properties).
UPSTREAM_BLOCK = """	tvsensor: thermal-sensor@14900500 {
		compatible = "hisilicon,luofu-tvsensor";
		reg = <0x14900500 0x100>;
		#thermal-sensor-cells = <0>;
		status = "okay";
	};
"""

W23_MARKERS = (
    "0x14900500 0x408",
    "hwver = <3>",
    "hisilicon,threshold-mc",
    "efuse-trim-en",
)


def filter_release_line(dts: str, source: str = "board DTS") -> str:
    count = dts.count(RELEASE_LINE_BLOCK)
    if count != 1:
        raise SystemExit(
            "%s: expected exactly one W23 release-line tvsensor block, found %d; "
            "update tools/dts-upstream-filter.py if the release-line DTS delta "
            "changed" % (source, count)
        )
    filtered = dts.replace(RELEASE_LINE_BLOCK, UPSTREAM_BLOCK)
    for marker in W23_MARKERS:
        if marker in filtered:
            raise SystemExit(
                "%s: W23 marker %r still present after filtering" %
                (source, marker))
    return filtered


def main(argv: list) -> int:
    args = [a for a in argv[1:] if a != "--check"]
    check_only = "--check" in argv[1:]
    if not args or len(args) > 2:
        raise SystemExit("usage: %s [--check] <target.dts> [<out.dts>]" % argv[0])
    src = Path(args[0])
    text = src.read_text()
    filtered = filter_release_line(text, str(src))
    if check_only:
        print("%s: release-line W23 block found and filtered cleanly" % src)
        return 0
    if len(args) == 2:
        Path(args[1]).write_text(filtered)
        print("%s -> %s (%d lines, %d removed)" %
              (src, args[1], len(filtered.splitlines()),
               len(text.splitlines()) - len(filtered.splitlines())))
    else:
        sys.stdout.write(filtered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
