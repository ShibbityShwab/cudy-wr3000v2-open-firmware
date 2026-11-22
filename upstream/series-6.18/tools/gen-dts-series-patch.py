#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Generate the upstream DTS-series entry (DTS-0001) from the single-source
board DTS.

The kernel review form (series/0001-0026.patch) is kernel-only.  SMP needs the
loader hand-off devicetree contract, so the DTS series entry is generated here
from target-skeleton/dts/luofu-r116.dts.

The single-source DTS is also the release-line DTS, so it carries the
release-line-only W23 thermal delta (target patches 066/067; not in the
upstream review form).  The board DTS placed in the patch is therefore the
output of tools/dts-upstream-filter.py, the single definition of the
release-line -> upstream DTS delta.  The generated patch adds that filtered
board DTS plus the hisilicon DTS Makefile entry.

Usage:
  tools/gen-dts-series-patch.py \
      --pristine  <extracted linux-6.18.60 tree> \
      --target-dts opensource/target-skeleton/dts/luofu-r116.dts \
      --out       opensource/upstream/series-6.18/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch

The generated patch is checked by tools/check-release-line.sh: it must apply,
compile, contain the loader hand-off contract and produce a board DTS that is
byte-identical to the target DTS.
"""

import argparse
import difflib
import subprocess
import sys
from pathlib import Path

FILTER = Path(__file__).resolve().with_name("dts-upstream-filter.py")


def filtered_dts(target_dts: Path) -> str:
    """Return the upstream board DTS (release-line-only delta stripped)."""
    if not FILTER.is_file():
        raise SystemExit("missing release-line filter: %s" % FILTER)
    out = subprocess.run([sys.executable, str(FILTER), str(target_dts)],
                         check=True, capture_output=True, text=True)
    return out.stdout


def build(pristine: Path, target_dts: Path) -> str:
    new = filtered_dts(target_dts).splitlines()
    mf_path = pristine / "arch/arm/boot/dts/hisilicon/Makefile"
    mf = mf_path.read_text().splitlines()

    idx = next((i for i, line in enumerate(mf)
                if line.startswith("dtb-$(CONFIG_ARCH_SD5203)")), None)
    if idx is None:
        raise SystemExit("insertion anchor dtb-$(CONFIG_ARCH_SD5203) not found")
    insert = [
        "dtb-$(CONFIG_ARCH_LUOFU) += " + chr(92),
        chr(9) + "luofu-r116.dtb",
    ]
    new_mf = mf[:idx] + insert + mf[idx:]

    mf_diff = list(difflib.unified_diff(
        [line + "\n" for line in mf],
        [line + "\n" for line in new_mf],
        fromfile="a/arch/arm/boot/dts/hisilicon/Makefile",
        tofile="b/arch/arm/boot/dts/hisilicon/Makefile",
        n=3))
    body = "".join(mf_diff[2:])

    out = ["--- /dev/null",
           "+++ b/arch/arm/boot/dts/hisilicon/luofu-r116.dts",
           "@@ -0,0 +1,%d @@" % len(new)]
    out += ["+" + line for line in new]
    out += ["--- a/arch/arm/boot/dts/hisilicon/Makefile",
            "+++ b/arch/arm/boot/dts/hisilicon/Makefile",
            body.rstrip("\n")]
    return "\n".join(out) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pristine", required=True, type=Path)
    ap.add_argument("--target-dts", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    text = build(args.pristine, args.target_dts)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    print("wrote %s (%d lines)" % (args.out, len(text.splitlines())))


if __name__ == "__main__":
    main()
