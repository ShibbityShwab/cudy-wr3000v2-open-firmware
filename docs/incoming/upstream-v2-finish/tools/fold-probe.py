#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host-only applicability probe for the luofu v2 review-form folds.

The v2 review form (30 patches) is defined by folding 16 L0 lab patches into
the frozen 26-patch v1 review form (see ../../upstream-v4/V2-REVIEW-FORM-STATUS.md
and ../wifi-upstream-target/submission-series.md).  Each fold is a source L0
patch whose hunks must be carried into a target v2 entry.  This tool answers,
with evidence instead of opinion: does each source hunk still apply at the
candidate v2 state?  It never edits the tree: every check is a git apply
--check on a scratch copy.

Method:
  1. copy the pristine linux-6.18.60 tree into <workdir>/tree and git-init it;
  2. branch "v1": apply the 26 review-form patches in order, one commit each,
     recording the commit id of every v1 entry state;
  3. branch "l0": apply the 66 L0 patches in order, one commit each, recording
     the commit id of the state after every L0 patch (the source patch's own
     native context);
  4. for every fold source patch: split it into file sections and hunks, then
     for each candidate base state (the v2 target entry state, the end-of-v1
     state, and the source's own L0 state) run git apply --check per hunk and
     for the whole patch.

Outputs <out>/fold-probe.json plus a markdown summary on stdout.  No network,
no device, no tree edit; the workdir is disposable.

Usage:
  python3 tools/fold-probe.py --pristine <linux-6.18.60> --workdir /tmp/fold-probe [--out DIR]
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LANE = HERE.parent
REPO = LANE.parents[3]
SERIES = REPO / "opensource" / "upstream" / "series-6.18" / "series"
L0 = (REPO / "opensource" / "target-skeleton" / "target" / "linux"
      / "hisilicon" / "patches-6.18")
NL = chr(10)

FOLDS = {
    "027": (["0019"], "6.18 clk-core read-only mux fix -> 0019 clock tree"),
    "031": (["0016", "0021", "0022", "0025"], "LSW EPS channel-map init -> LSW group"),
    "032": (["0025", "0026"], "EQM queue-manager init -> new RFC 0027"),
    "036": (["0025", "0026"], "PFE init -> new RFC 0028"),
    "038": (["0009", "0023"], "GEMAC max-frame init -> 0009/0023"),
    "041": (["0009", "0023"], "GEMAC MAC-config replay -> 0009/0023"),
    "043": (["0025", "0026"], "IFC default table -> new RFC 0028"),
    "044": (["0022"], "final luofu_switch.* option names -> 0022"),
    "046": (["0025", "0026"], "IFC EPS extension -> new RFC 0028"),
    "048": (["0020"], "PIE RXCFG RX-enable word -> 0020"),
    "051": (["0009", "0023"], "GEMAC static MAC config words -> 0009/0023"),
    "053": (["0024"], "vendor-exact PIE RX completion decode -> 0024"),
    "059": (["0026"], "NAPT flow-engine init -> new RFC 0029"),
    "060": (["0026"], "PIE exception-class delivery -> new RFC 0030"),
    "066": (["0012"], "thermal v1/hwver-3 read path -> 0012"),
    "067": (["0012"], "thermal binding update -> 0012"),
}

GIT_ENV = {
    "GIT_AUTHOR_NAME": "fold probe",
    "GIT_AUTHOR_EMAIL": "fold-probe@example.invalid",
    "GIT_COMMITTER_NAME": "fold probe",
    "GIT_COMMITTER_EMAIL": "fold-probe@example.invalid",
}


def run(cmd, cwd, check=True):
    env = dict(os.environ)
    env.update(GIT_ENV)
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env)
    if check and p.returncode != 0:
        sys.exit("FAILED " + " ".join(cmd) + " (cwd " + str(cwd) + "): " +
                 (p.stdout or "") + (p.stderr or ""))
    return p


def git(repo, *args, check=True):
    return run(["git"] + list(args), repo, check=check)


def l0_files():
    return sorted(L0.glob("*.patch"))


def l0_number(path):
    return path.name.split("-", 1)[0]


def split_sections(text):
    """Split a unified diff into [(file_header_lines, [hunk_lines...]), ...]."""
    lines = text.splitlines(keepends=True)
    sections = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("diff --git "):
            start = i
            i += 1
            while i < len(lines) and not lines[i].startswith("--- "):
                i += 1
            if i + 1 < len(lines) and lines[i + 1].startswith("+++ "):
                header = lines[start:i + 2]
                i += 2
            else:
                sys.exit("unparsable section after diff --git")
        elif (line.startswith("--- ") and i + 1 < len(lines)
              and lines[i + 1].startswith("+++ ")):
            header = lines[i:i + 2]
            i += 2
        else:
            i += 1
            continue
        hunks = []
        cur = None
        while i < len(lines):
            cur_line = lines[i]
            if cur_line.startswith("@@ "):
                if cur is not None:
                    hunks.append(cur)
                cur = [cur_line]
                i += 1
                continue
            if cur_line.startswith("diff --git "):
                break
            if (cur_line.startswith("--- ") and i + 1 < len(lines)
                    and lines[i + 1].startswith("+++ ")):
                break
            if cur is not None:
                cur.append(cur_line)
            i += 1
        if cur is not None:
            hunks.append(cur)
        sections.append((header, hunks))
    return sections


def hunk_header(header):
    return [line for line in header
            if not line.startswith("diff --git ")
            and not line.startswith("index ")
            and not line.startswith("new file mode")]


def write_hunk(path, header, hunk):
    with open(path, "w") as fh:
        for line in header:
            fh.write(line)
        for line in hunk:
            fh.write(line)


def setup_tree(pristine, workdir):
    tree = workdir / "tree"
    if tree.exists() and (tree / ".git").is_dir():
        # reuse the scratch repo: return to its root (pristine) commit
        roots = git(tree, "rev-list", "--max-parents=0", "HEAD").stdout.split()
        if not roots:
            sys.exit("scratch repo has no root commit")
        git(tree, "checkout", "-q", roots[0])
        git(tree, "clean", "-qfd")
        return tree, roots[0]
    if not tree.exists():
        print("copying pristine tree -> " + str(tree), file=sys.stderr)
        shutil.copytree(pristine, tree, symlinks=True)
    git(tree, "init", "-q")
    git(tree, "config", "user.name", "fold probe")
    git(tree, "config", "user.email", "fold-probe@example.invalid")
    git(tree, "add", "-A")
    p = git(tree, "commit", "-q", "-m", "pristine linux-6.18.60", check=False)
    if p.returncode != 0 and "nothing to commit" not in (p.stdout + p.stderr):
        sys.exit("baseline commit failed: " + p.stdout + p.stderr)
    return tree, git(tree, "rev-parse", "HEAD").stdout.strip()


def apply_commit(tree, patch_path):
    p = git(tree, "apply", str(patch_path), check=False)
    if p.returncode != 0:
        p = run(["patch", "-p1", "--fuzz=0", "-s", "-i", str(patch_path)], tree,
                check=False)
        if p.returncode != 0:
            sys.exit("apply failed for " + str(patch_path) + ": " +
                     (p.stdout or "") + (p.stderr or ""))
        for orig in tree.rglob("*.orig"):
            orig.unlink()
    git(tree, "add", "-A")
    git(tree, "commit", "-q", "-m", "apply " + Path(patch_path).name)
    return git(tree, "rev-parse", "HEAD").stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pristine", required=True)
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    pristine = Path(args.pristine).resolve()
    workdir = Path(args.workdir).resolve()
    out = Path(args.out).resolve() if args.out else LANE / "validation"
    out.mkdir(parents=True, exist_ok=True)
    workdir.mkdir(parents=True, exist_ok=True)
    if not (pristine / "Makefile").is_file():
        sys.exit("not a kernel tree: " + str(pristine))

    tree, pristine_commit = setup_tree(pristine, workdir)
    states = {"pristine": pristine_commit}

    git(tree, "checkout", "-q", "-B", "v1", pristine_commit)
    v1_patches = sorted(SERIES.glob("*.patch"))
    if len(v1_patches) != 26:
        sys.exit("expected 26 v1 series patches, found %d" % len(v1_patches))
    for p in v1_patches:
        states[p.name[:4]] = apply_commit(tree, p)
    states["v1end"] = git(tree, "rev-parse", "HEAD").stdout.strip()

    git(tree, "checkout", "-q", "-B", "l0", pristine_commit)
    l0 = l0_files()
    if len(l0) != 66:
        sys.exit("expected 66 L0 patches, found %d" % len(l0))
    for p in l0:
        states["l0pre@" + l0_number(p)] = git(tree, "rev-parse", "HEAD").stdout.strip()
        states["l0@" + l0_number(p)] = apply_commit(tree, p)

    hunksdir = workdir / "hunks"
    if hunksdir.exists():
        shutil.rmtree(hunksdir)
    hunksdir.mkdir(parents=True)
    results = {}
    for num in sorted(FOLDS):
        targets, note = FOLDS[num]
        srcs = [p for p in l0 if l0_number(p) == num]
        if not srcs:
            sys.exit("missing fold source patch " + num)
        src = srcs[0]
        sections = split_sections(src.read_text())
        candidates = ["l0pre@" + num] + ["v1:" + t for t in targets] + ["v1end"]
        entry = {"source": src.name, "targets": targets, "note": note,
                 "candidates": {}, "hunk_count": 0}
        for ci, cand in enumerate(candidates):
            state = states[cand.split(":", 1)[1]] if cand.startswith("v1:") else states[cand]
            git(tree, "checkout", "-q", state)
            per_hunk = []
            hi = 0
            for header, hunks in sections:
                hdr = hunk_header(header)
                for hunk in hunks:
                    hi += 1
                    hpath = hunksdir / ("%s-%02d-%02d.patch" % (num, ci, hi))
                    write_hunk(hpath, hdr, hunk)
                    p = git(tree, "apply", "--check", str(hpath), check=False)
                    body = "".join(hunk)
                    per_hunk.append({
                        "hunk": hi,
                        "file": header[-1].split(" b/")[-1].strip(),
                        "header": hunk[0].strip(),
                        "gated": ("module_param" in body or "_gate" in body
                                  or " gate" in body),
                        "ok": p.returncode == 0,
                        "error": (p.stderr or p.stdout).strip().splitlines()[:2],
                    })
            p = git(tree, "apply", "--check", str(src), check=False)
            entry["candidates"][cand] = {
                "commit": state,
                "whole_patch_ok": p.returncode == 0,
                "whole_patch_error": (p.stderr or p.stdout).strip().splitlines()[:2],
                "hunks_ok": sum(1 for h in per_hunk if h["ok"]),
                "hunks_total": len(per_hunk),
                "hunks": per_hunk,
            }
        entry["hunk_count"] = max(c["hunks_total"] for c in entry["candidates"].values())
        results[num] = entry

    lines = ["# Fold applicability probe (host-only, git apply --check per hunk)", "",
             "Source: L0 fold-source patches -> candidate v2 base states.",
             "native = the source patch's own L0 context; v1:<entry> = the frozen",
             "review-form state after that entry; v1end = after all 26 v1 entries.",
             "Generated by tools/fold-probe.py; raw per-hunk detail: fold-probe.json.",
             "",
             "| fold | source | candidate | hunks apply | whole patch |",
             "| --- | --- | --- | --- | --- |"]
    for num in sorted(results):
        entry = results[num]
        for cand in entry["candidates"]:
            cres = entry["candidates"][cand]
            lines.append("| %s | %s | %s | %d/%d | %s |"
                         % (num, entry["source"][:4], cand, cres["hunks_ok"],
                            cres["hunks_total"],
                            "yes" if cres["whole_patch_ok"] else "no"))
    lines += ["", "Per-hunk errors are in fold-probe.json.", ""]
    (out / "fold-probe.md").write_text(NL.join(lines))
    (out / "fold-probe.json").write_text(json.dumps({
        "pristine": str(pristine),
        "v1end_state": states["v1end"],
        "folds": results,
    }, indent=1))
    print(NL.join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
