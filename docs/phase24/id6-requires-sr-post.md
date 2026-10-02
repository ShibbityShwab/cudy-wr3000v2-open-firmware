# Localized: the id-6 word requires an SR descriptor post (phase 24h, 2026-10-02)

The 25 ms poll settled the question, and the answer **localizes the difference to a specific host
action**.

## The clean negative

Evidence `build/register-dumps/exp/20261002-213111/`, `pollms=25 polldur=4000`, poll starting at the
instant of the release:

```
omo-drv1: ---- post-release mailbox poll (25 ms interval, 4000 ms total) ----
omo-drv1:   t=0  out[0]=0x00000000 out[1]=0x00000000 (baseline)
omo-drv1:   t=550ms out[0] 0x00000000 -> 0x00000000, out[1] 0x00000000 -> 0x00000004
omo-drv1:     out[1] bit 2 set (id 2)
omo-drv1:   poll done: 1 transitions in 4000 ms; final out[0]=0x00000000 out[1]=0x00000004
```

**One transition, id 2, at +550 ms - and no `0x40` (id 6) at all.** So the id-6 word is *genuinely
absent* in this configuration rather than hidden by my instrumentation: 25 ms resolution starting at
the release rules the resolution and ordering hypotheses out. The poll blind spot was real and worth
closing, but it was not hiding the word.

## The difference, found by diffing the two runs

Phase 20's `fw-accept` run - the only one in this project that ever saw the id-6 word - did this before
it appeared (`docs/phase20/fw-accept.md`):

```
ENABLE SR ch0 +0x00 0x00000001 -> 0x00000001 readback=0x00000001
ENABLE SR ch0 +0x48 0x00000001 -> 0x00000001 readback=0x00000001
[post0 +430ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 = SR engine read
[send post0] pcie_msg_send(chip,3): out[0] 0x00000000 -> 0x00000008 readback=0x00000008
[poll +910ms] out[1] = 0x00000040   <- id 6, ONLY after the SR engine consumed a descriptor
```

Its own summary lists "the DR post + producer commit, **the SR post** (the vendor's 72-byte id-1 frame
in slot 0, the alg frame in slot 1) + producer commit".

`wifidrv1` writes the ring **configuration** and stops there - by explicit design, and it says so:

```
omo-drv1: NOTE no descriptor submitted, no doorbell rung (bounded by design)
```

**That is the difference.** The firmware's `pcie_trigger_ete_sending_handle` (id 6) - "wake the host's
receive thread" - fires only after the host has posted an SR descriptor and the device's SR engine has
consumed it. Without a descriptor there is nothing to wake a thread about, so the firmware stays at
id 2. Phase 22's conclusion that "submitting without the device-side accept gate does nothing" was
drawn from a ring-ownership-only configuration; the reverse dependency is the one that matters here -
the device does not advance *until* a descriptor is posted.

## The next step, and why it is now well-posed

Port the SR post into `wifidrv1` from `lab/fwaccept/fwaccept.c` (which still has it):
`shuangta_ete_sr_dscr_fill` for the node, then the producer-index commit to SR `+0x18`/`+0x38`, plus
the channel enable at `+0x00`/`+0x48`. That runs the whole dialogue - correct registers, fine poll, the
service, and a posted descriptor - in one boot for the first time, and it tests whether id 6 appears
where it is now expected.

The experiment is well-posed because both outcomes are decisive:

- **id 6 appears** - the firmware's dialogue is two words, the single-word reading of phases 19/23/24 was
  a consequence of never posting a descriptor, and the host has been answering the second sentence.
- **id 6 does not appear** - the descriptor post is necessary but not sufficient, and the remaining
  difference from phase 20's run is narrowed to the `pcie_msg_send(chip,3)` ordering or the outbound
  iATU viewport.
