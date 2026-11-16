# Publish readiness (2026-11-17)

**Status: NOT READY.** This page records what the published half contains, what was fixed on
2026-11-17, and the checklist that must pass before this repository is made public or cited as
the basis of an upstream submission. The live bench state is the parent repo's `STATUS.md`;
this file is about publication, not about the router.

## What was wrong (audit 2026-11-17)

1. **Ten vendor object files were tracked** under `lab/luofu-kernel/nand/` (`triductor/*.o`,
   `nfc_tr6560/*.o`, `parsers/ofpart_triductor.o`). They are vendor GPL object code from a
   TR6560 BSP build (DWARF paths under `/home1/develop/release/...`), not our work, and they
   contradicted `NOTICE.md` ("no vendor kernel modules") and `README.md` ("no vendor
   binaries"). They were removed from HEAD on 2026-11-17.
2. **They are still in the published history** (root commit `a2d4c8a`). Removing them from
   HEAD is not enough: the history must be purged (filter-repo/filter-branch) before this
   repository is public.
3. **`GPL-REQUEST.md` said "draft, ready to send"**, while `PORT-SURFACE.md`,
   `PORT-PLAN.md` and the parent ledger say the request was sent 2026-10-01 with no reply.
   Status reconciled on 2026-11-17: sent, no reply held, no copy/receipt in the repo.
4. **The repository-level license was MIT only**, while every lab kernel module is
   `GPL-2.0` (SPDX headers + `MODULE_LICENSE("GPL")`). The split is now stated in
   `README.md` (MIT for scripts/docs, GPL-2.0 for kernel code); a GPL-2.0 license text and
   per-file copyright/DCO sign-offs are still missing.
5. **"Clean-room" was an inaccurate label** for drivers transcribed from disassembly of
   vendor/BSP GPL binaries. The provenance is GPL-compatible reverse engineering; the
   submission must say so, and the derived drivers must be GPL-2.0 with attribution.
6. **No upstream artifact exists**: there is no `target/linux/luofu/`, no upstream-style DTS,
   no image recipe, no kernel patch series, no modern-kernel driver. The roadmap is a plan, not
   progress. Bench gates (G1-G6 in the parent `STATUS.md`) are not upstream artifacts.

## Publish checklist (must all be true before public)

- [ ] Vendor `.o/.ko` purged from **history**, not just HEAD (`git filter-repo`); verify with
      `git rev-list --objects --all | grep -E '\.(o|ko)$'` returning nothing.
- [ ] No other vendor binaries/blobs/calibration/dumps anywhere in history.
- [ ] `GPL-REQUEST.md` holds the sent message + receipt (or a dated "no reply" record) and is
      referenced from `NOTICE.md`.
- [ ] License split implemented: `COPYING` (GPL-2.0) + `LICENSE` (MIT) + per-file SPDX and
      Copyright for `lab/`; DCO sign-off convention documented.
- [ ] Provenance statement for the reverse-engineered drivers ("transcribed from disassembly
      of the vendor's GPL binaries; GPL-2.0") replaces "clean-room".
- [ ] Reproducible artifact: a tracked image-building recipe that regenerates the 40,732-byte
      FIP/DTBO trailer from a locally supplied stock image, plus the kernel delta patch series
      (the private mirror currently has changes not published: near-region `module_alloc`,
      jffs2/ubi hooks, initramfs init).
- [ ] A single-source, upstream-style DTS (the current ones are skeletons/multiple copies) and
      the bindings for any submitted driver.
- [ ] `target/linux/luofu/` with image recipe + profile + board files (none exists).
- [ ] Wi-Fi and LAN-switch statement: vendor-only today; firmware blob license unresolved; GPL
      source request unanswered. The submission cannot claim a complete open port without them.

## What can be published today

The documentation of the reverse-engineering method and the hardware findings, with the
vendor-derived artifacts excluded, and with the roadmap clearly labelled aspirational. The
parent repository (private) is the live record.
