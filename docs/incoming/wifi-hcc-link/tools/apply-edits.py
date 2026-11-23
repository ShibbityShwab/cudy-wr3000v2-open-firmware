#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""apply-edits.py - deterministic integration edits for the wifi-hcc-link
extension (host-side HCC link/event observability + id-1 wake ladder).

Usage: apply-edits.py <tree-dir> <files-dir>

The tree must ALREADY contain the wifi-h2d-inject patch
(../wifi-h2d-inject/patches/0001-hi5622-pciet-h2d-inject-b3.patch): this lane's
patch 0002 is generated/applied on top of it.  Every edit asserts its anchor
occurs exactly once, so a changed reference tree fails loudly instead of
producing a silently wrong patch.  gen-patches.sh runs this on a copy of the
post-0001 b3 tree (../wifi-transport-port/b3) and diffs the pair.
"""
import pathlib
import shutil
import sys

HDR_ANCHOR = "\n#endif /* _HI5622_PCIET_H */\n"
HDR_ADD = """
/* ---------------------------------------------------------------------
 * 11. HCC link/event observability + H2D consumer-liveness ladder
 *     (wifi-hcc-link lane; ../wifi-hcc-link/ - see REPORT.md sec 3-5)
 *
 * The vendor host sends NO link-create/register/event-register H2D message;
 * this surface only READS the firmware link/event objects and posts the one
 * vendor-attested payload-free mailbox id-1 wake.  See pciet_hcc_link.h.
 * ------------------------------------------------------------------- */
/* pciet_hcc_link.c: link/event state + bounded id-1 wake probe. */
int  pciet_hcc_link_probe(struct hi5622_pci *ctx);
void pciet_hcc_link_remove(struct hi5622_pci *ctx);
"""

PROBE_ANCHOR = "\n\treturn 0;\n\nerr_sr0:\n"
PROBE_ADD = """
	/* wifi-hcc-link lane: HCC link/event observability + id-1 wake ladder
	 * (hcc_link_* sysfs attrs; host-side allocation only, no device write
	 * in probe). */
	if (pciet_hcc_link_probe(ctx))
		dev_warn(dev, "HCC link/event surface probe failed; hcc_link_* attrs unavailable\\n");

	return 0;

err_sr0:
"""

REMOVE_ANCHOR = "\tdevice_remove_groups(&pdev->dev, pciet_groups);\n\tpciet_h2d_remove(ctx);\n\tpciet_poll_stop(ctx);\n"
REMOVE_ADD = "\tdevice_remove_groups(&pdev->dev, pciet_groups);\n\tpciet_h2d_remove(ctx);\n\tpciet_hcc_link_remove(ctx);\n\tpciet_poll_stop(ctx);\n"

KBUILD_OLD = "hi5622_pciet_b3-y := pciet_main.o pciet_ete.o pciet_b3.o pciet_h2d.o\n"
KBUILD_NEW = "hi5622_pciet_b3-y := pciet_main.o pciet_ete.o pciet_b3.o pciet_h2d.o pciet_hcc_link.o\n"

NEW_FILES = ("pciet_hcc_link.c", "pciet_hcc_link.h")


def patch(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"ERROR: {path}: anchor occurs {count} times, expected 1")
    path.write_text(text.replace(old, new))


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    tree = pathlib.Path(sys.argv[1])
    files = pathlib.Path(sys.argv[2])
    for name in NEW_FILES:
        shutil.copyfile(files / name, tree / name)
    patch(tree / "hi5622_pciet.h", HDR_ANCHOR, HDR_ADD + HDR_ANCHOR)
    patch(tree / "pciet_main.c", PROBE_ANCHOR, PROBE_ADD)
    patch(tree / "pciet_main.c", REMOVE_ANCHOR, REMOVE_ADD)
    patch(tree / "Kbuild", KBUILD_OLD, KBUILD_NEW)
    print(f"apply-edits: {tree} integrated (2 new files + 3 edit sites)")


if __name__ == "__main__":
    main()
