# Tested-by policy - submission package

**No upstream Tested-by/Reviewed-by/Acked-by is claimed for this series.**

- No third party has tested or reviewed this code in an upstream context.  This
  package must not be sent with any Tested-by/Reviewed-by/Acked-by that was not
  given by an independent party for the exact posted objects.
- The only hardware evidence is **target-image bench evidence**: DTS-purpose
  image **6e9c54d0** (flashinfo compatible-first; 2 CPUs, cmdline
  ubi.mtd=rootfsb + project marker, no errors; DTB 12,678 B sha256
  `62574b11...`, padded 12,680 B sha256 `8d79794e...`), previous
  DTS-purpose baseline c3ef2a50 (12,650/12,656 B), and earlier baselines
  f6685b80/7421bb7a.  That is notebook evidence [V], not an upstream Test-by,
  and the series is an RFC.
- Host-side checks (sequential patch apply, dtc compile, loader-contract check,
  pad-dtb, checkpatch) are necessary but **not sufficient**: the rebuilt image
  must be boot-tested on the unit after any active-DTS change before it can be
  treated as hardware-validated.  The superseded active cleanup 538377c is a
  concrete counter-example (it passed every host check and did not boot).
- If a future independent tester offers a Tested-by, it must name the exact
  posted series/object hashes, the hardware revision and the tested tree; it
  cannot be inferred from the notebook evidence.
- This policy also covers the DTS-series entry (DTS-0001) and the board DTS;
  the loader hand-off contract is measured from the stock loader, but that is
  not a Test-by of the DTS patch.
