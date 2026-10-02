# The first ~540 ms after the release were never observed (phase 24g, 2026-10-02)

A replay of my own measurements against the record turned up a **blind spot** that may have hidden the
first word of the firmware's dialogue.

## The discrepancy

Phase 20 (`docs/phase20/host-window.md`, `fw-accept.md`) recorded **three** transitions on the pending
word, from a run that also serviced it:

```
[poll +910ms]  out[1] = 0x00000040   bit 6 (id 6 = pcie_trigger_ete_sending_handle)
[poll +1410ms] out[1] = 0x00000004   bit 2 (id 2)
[poll +1620ms] out[1] = 0x00000000
```

Every run of mine in phases 23/24 captured **only** the `0x04` and never the `0x40` - e.g.
`t=500ms out[0] 0x00000000 -> 0x00000000, out[1] 0x00000000 -> 0x00000004`. And phase 20 is explicit
about what bit 6 means: `pcie_trigger_ete_sending_handle` is a 4-byte tail call to
`pcie_wkup_thread` - **"wake the host's HCC receive thread."** If the firmware says that first and I
never see it, then I have been reasoning about the dialogue from its second sentence.

## Two candidate causes, both in my instrumentation

1. **Ordering blind spot.** The init path was: `release -> msleep(500) -> sig_read("post") (~40 ms) ->
   poll`. The mailbox was therefore **unobserved for the first ~540 ms after the release** - and phase
   20 puts the id-6 word at ~+910 ms from a poll that had no such delay. Not a proof of overlap, but a
   needless hole exactly where the interesting transition lives.
1. **Resolution.** The poll interval was 500 ms. A word that appears at +910 ms and is replaced at
   +1410 ms is *visible* at 500 ms resolution only if a sample lands inside it; a shorter-lived word
   would be missed outright.

## The fix

`release -> poll -> sig_read("post")` (commit `c9299b8`). Nothing about the poll needs the signature to
run first, and the signature is CPU *state* rather than a transient - `dcoldo_vset` stays changed and
the BSS stays zeroed for as long as the CPU runs - so reading it after the poll window is equally
valid. The poll interval is a module parameter (`pollms`) and the confirmation run uses **25 ms over
4 s**, which resolves the whole sequence with 20x the previous resolution.

## Why this matters more than a re-measurement

If the id-6 word is present and was simply being missed, then the firmware's dialogue is *two* words
rather than one, and the device is explicitly asking the host to service its receive path before it
does anything else. That changes the reading of every "the device does not accept" result in phases
20-24: the host may have been answering the wrong sentence.

If the id-6 word is genuinely absent in my configuration, that is also informative - it means the
trigger for `pcie_trigger_ete_sending_handle` depends on something phase 20's run had and mine does
not, which is a concrete, findable difference.

Either outcome is a real result; the point is that the previous measurement could not distinguish them.
