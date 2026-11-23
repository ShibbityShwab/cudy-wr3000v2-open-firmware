#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""apply-edits.py - deterministic integration edits for the H2D injector.

Usage: apply-edits.py <tree-dir> <base|b3> <files-dir>

Applies the small hand-written integration deltas that turn the reference
transport tree (../wifi-transport-port/build for "base", ../wifi-transport-port/b3
for "b3") into the injector build, and copies the new source files in.
Every edit asserts its anchor occurs exactly once, so a changed reference tree
fails loudly instead of producing a silently wrong patch.  gen-patches.sh runs
this twice (on two copies of each tree) to regenerate patches/; build.sh only
uses the committed patches.
"""
import pathlib
import shutil
import sys

HDR_ANCHOR = "\n#endif /* _HI5622_PCIET_H */\n"
HDR_ADD = """
/* ---------------------------------------------------------------------
 * 10. H2D frame injector (wifi-h2d-inject lane; ../wifi-h2d-inject/)
 * ------------------------------------------------------------------- */
/* pciet_main.c: the single gated device-write helper, exported so the
 * injector's mailbox/ETE writes still pass the never-write out[5] gate. */
void pciet_writel_checked(struct hi5622_pci *ctx, u32 bar0_off, u32 val);

/* pciet_h2d.c: userspace-driven, validated H2D frame injector. */
int  pciet_h2d_probe(struct hi5622_pci *ctx);
void pciet_h2d_remove(struct hi5622_pci *ctx);
"""

PROBE_ANCHOR = "\treturn 0;\n\nerr_sr0:\n"
PROBE_ADD = """\t/* wifi-h2d-inject lane: userspace H2D frame injector (h2d_* sysfs
\t * attrs; host-side allocations only, no device writes in probe). */
\tif (pciet_h2d_probe(ctx))
\t\tdev_warn(dev, "H2D injector probe failed; h2d_* attrs unavailable\\n");

\treturn 0;

err_sr0:
"""

REMOVE_ANCHOR = "\tdevice_remove_groups(&pdev->dev, pciet_groups);\n\tpciet_poll_stop(ctx);\n"
REMOVE_ADD = "\tdevice_remove_groups(&pdev->dev, pciet_groups);\n\tpciet_h2d_remove(ctx);\n\tpciet_poll_stop(ctx);\n"

KBUILD = {
    "base": ("hi5622_pciet-y := pciet_main.o pciet_ete.o\n",
             "hi5622_pciet-y := pciet_main.o pciet_ete.o pciet_h2d.o\n"),
    "b3": ("hi5622_pciet_b3-y := pciet_main.o pciet_ete.o pciet_b3.o\n",
           "hi5622_pciet_b3-y := pciet_main.o pciet_ete.o pciet_b3.o pciet_h2d.o\n"),
}

NEW_FILES = ("pciet_h2d.c", "pciet_h2d_frame.h")


def patch(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"ERROR: {path}: anchor occurs {count} times, expected 1")
    path.write_text(text.replace(old, new))


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    tree = pathlib.Path(sys.argv[1])
    kind = sys.argv[2]
    files = pathlib.Path(sys.argv[3])
    if kind not in KBUILD:
        raise SystemExit(f"ERROR: unknown tree kind {kind!r}")
    patch(tree / "hi5622_pciet.h", HDR_ANCHOR, HDR_ADD + HDR_ANCHOR)
    patch(tree / "pciet_main.c",
          "static void pciet_writel_checked(",
          "void pciet_writel_checked(")
    patch(tree / "pciet_main.c", PROBE_ANCHOR, PROBE_ADD)
    patch(tree / "pciet_main.c", REMOVE_ANCHOR, REMOVE_ADD)
    old, new = KBUILD[kind]
    patch(tree / "Kbuild", old, new)
    for name in NEW_FILES:
        shutil.copyfile(files / name, tree / name)
    print(f"applied h2d-integration edits to {tree} ({kind} tree)")


if __name__ == "__main__":
    main()
