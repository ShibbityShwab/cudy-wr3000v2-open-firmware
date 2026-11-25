# CONTRIBUTING - licence, provenance and DCO conventions

This repository is a reverse-engineering research notebook for the Cudy WR3000
v2.0 (PCB WR3000V2-R116, HiSilicon Hi5671YV200 "luofu" + Hi5622V100 Wi-Fi).
Contributions must keep it publishable: no vendor binaries, no secrets, no
unlicensed code.

## Licensing split

| part | licence | text |
| --- | --- | --- |
| scripts, build recipes, documentation (`tools/`, `docs/`, `target-skeleton/tools/`, `upstream/series-6.18/tools/`, most of the tree) | MIT | [LICENSE](LICENSE) |
| kernel code and kernel-derived work (`lab/`, `target-skeleton/target/linux/`, `upstream/series-6.18/series/`) | GPL-2.0-only | [COPYING](COPYING) |

Every new kernel source file must carry an `SPDX-License-Identifier` tag on its
first line (or second line after a shebang); use `GPL-2.0-only` for kernel
code and `GPL-2.0-only` for shell/python tooling that is copied into the
kernel build, and MIT only for new notebook-only scripts that are not derived
from kernel sources.  Do not add an SPDX tag to a file whose provenance you
cannot state.

## Provenance

The kernel drivers were derived by reverse-engineering the vendor's GPL
binaries and the stock device tree.  They are **GPL-compatible reverse
engineering, not "clean-room from documents"**: the source of the measured
behaviour is in this repository's evidence trail (see `docs/PUBLISH-READINESS.md`
and `docs/PUBLISH-PREP.md`).

Two standing rules:

1. **Never commit vendor binaries or opaque data.**  Vendor firmware, ROM
   dumps, calibration/EFUSE data, vendor `.o`/`.ko` files, extracted vendor
   DTS and per-unit MAC/serial values stay out of tracked text (local evidence
   directories are gitignored).  Identities are recorded as SHA-256.
2. **Never commit secrets.**  No credentials, keys, tokens, or live per-unit
   values; use placeholders and the gitignored `.omo-secrets/` directory.

## Developer Certificate of Origin and Signed-off-by

By contributing you certify the **Developer Certificate of Origin** (DCO),
version 1.1 - the same text used by the Linux kernel and OpenWrt: you have the
right to submit the work under the project's licence and you agree to the
certification in <https://developercertificate.org>.

Every commit must carry a `Signed-off-by` trailer matching the author:

    git commit -s -m "subsystem: short summary"

This is the `Signed-off-by: Name <email>` line `git commit -s` appends; it is
the DCO sign-off, not a Reviewed-by/Acked-by claim.  Pre-existing history
(before this convention was written) has no Signed-off-by trailers; the owner
decides how that legacy history is handled on publication (fresh curated
repositories are the recommended path - see `docs/PUBLISH-PREP.md`).

## Checks before a commit

- `tools/check-repo.sh` (parent repository) - repo consistency guards;
- `cd opensource && bash tools/publish-prep-check.sh` - publication readiness
  (read-only; FAIL is the honest current state until the owner closes the
  history/licence items);
- `cd upstream/series-6.18 && python3 tools/gen-series.py --check` - kernel
  review-form drift;
- the lane-specific host tests documented next to the change.

## Reporting security issues

Do not paste live credentials, private keys or vendor binaries into an issue.
The notebook's publication policy (`docs/PUBLISH-READINESS.md`) applies to
issues and pull requests as well as to committed text.
