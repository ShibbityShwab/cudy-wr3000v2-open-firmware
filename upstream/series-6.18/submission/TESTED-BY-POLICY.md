# Tested-by policy - submission package

**No upstream Tested-by/Reviewed-by/Acked-by is claimed for this series.**

- No third party has tested or reviewed this code in an upstream context.  This
  package must not be sent with any Tested-by/Reviewed-by/Acked-by that was not
  given by an independent party for the exact posted objects.
- The only hardware evidence is **target-image bench evidence**: DTS-purpose
  image **f6685b80** (boot-tested TWD level-high delta; 2 CPUs, working LAN via
  the documented CPU-copy bench workaround, SSH, persistent ubifs overlay;
  DTB 13,041 B sha256 `4fde76df...`, padded 13,048 B sha256
  `4b50de73...`); previous verified baseline image 7421bb7a.  That is
  notebook evidence [V], not an upstream Test-by, and the series is an RFC.
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
