# wifi-5ghz-bringup - 5 GHz endpoint readiness + one-radio/two-band wiphy

**Lane:** wifi-5ghz-bringup (shared task **task-154**), HOST-ONLY clean-room.
**Written:** 2026-11-25. **Status:** analysis + patch + host tests + fire
recipe delivered; module built and host-verified; **bench NOT fired** (the
Lead owns the fire and the recovery door).

This lane builds on the FINAL ASSOCIATION module (stripped 344,372 B sha256
ab9e18a4..., 56 params) and answers the 5 GHz fire's G0-precondition gap from
the ledger entry 2026-11-29 07:00.

## TL;DR

* **Refuted:** "endpoint 0001 needs its own ready handshake/boot ladder". The
  two PCI functions are two **host views of one chip 0**: their BAR0 windows
  alias the same register space and the vendor initialises/servics only
  instance 0. A second ladder would re-upload/re-release the same firmware
  through the other RC - unsafe and unnecessary.
* **Root cause:** the wiphy registration gate runs on each PCI context's own
  HCC core; only the booted primary endpoint (domain 0) reaches READY, so the
  5 GHz wiphy allocated on 0001 was never registered.
* **Fix:** with band5g=1 the **primary endpoint's one wiphy carries BOTH
  bands** (2.4 GHz + vendor 29-channel 5 GHz profile); the secondary host view
  gets no wiphy; AP/scan band bytes come from the cfg80211 chandef/requested
  channels. band5g=0 (bench-validated 2.4 GHz path) is unchanged.
* **Deliverable module:** stripped 351,008 B sha256
  1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53, 57 params,
  0 warnings, two builds byte-identical; selftest 114/114 + 20 static checks.

## Read in this order

1. [REPORT.md](REPORT.md) - the analysis, the evidence, the fix, the limits.
2. [TABLES.md](TABLES.md) - endpoints/wiphy plan/gate/band-byte/artifact tables.
3. [FIRE-RECIPE.md](FIRE-RECIPE.md) - the attended G0-G7 ladder (NOT FIRED).
4. [BUILD-NOTES.md](BUILD-NOTES.md) - how the module is rebuilt and verified.

## Run the host checks (no device)

    ./tools/run-host-tests.sh     # portable selftest + patch static checks
    ./tools/build-5ghz-ready.sh   # full chain rebuild + artifact contract

## Non-negotiable

Never run the boot ladder on 0001:00:00.0; never rmmod after the CPU release;
no UART transmit; no vendor binary/firmware/calibration in tracked text; the
Lead fires, attended, with the recovery door armed.

## Files

| path | contents |
| --- | --- |
| REPORT.md | analysis + fix + evidence |
| TABLES.md | evidence tables |
| FIRE-RECIPE.md | attended ladder design |
| BUILD-NOTES.md | build chain, artifact identity, host tests |
| patches/0001-band5g-assoc.patch | band5g profile rebased for the association tree |
| patches/0002-5ghz-primary-wiphy.patch | one-radio/two-band wiphy fix |
| src/hi5622_band5g.c/.h | portable band/plan helpers (host-tested) |
| src/band5g_selftest.c | 114-check portable selftest |
| tools/build-5ghz-ready.sh | chain build + contract checks |
| tools/run-host-tests.sh | selftest + patch static checks |
| ladders/ | hostapd 5 GHz configs + on-router fire script |
| evidence/ | captured host output (text only) |
