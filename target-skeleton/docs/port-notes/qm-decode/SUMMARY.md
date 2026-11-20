# incoming/qm-decode - round 4p: mirror sweep fix, static CPU-copy, RX-stroke (patch 055)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 21).  Prior: `032`...`054`.
This package delivers **patch 055**: the corrected mirror sweep (all encodable destinations), the
stable static CPU-copy gate (W18), and the RX-stroke instrumentation needed to explain why the
mirror run got ICMP replies with zero RXDIAG/RXERR lines.  No device access.

## 1. W17 mirror run (image e9de3f80)

- Sweep programmed dst 4 (3.2s), 5, 6, 7, 8 (11.3s) then completed at 13.3s; **40..43 were never
  tried** - not only by the candidate list but because the decoded mirror destination field is only
  4 bits ((dst & 0xf) << 4), so ports 40..43 cannot be encoded at all.  Patch 055 makes that
  explicit and sweeps every encodable destination instead.
- Host ping: 4/18 replies (seq4 1024 ms = ARP resolution, seq5-7 ~0.15 ms); the sniff shows the box
  ARP and ICMP echo replies.  The mirror configuration active at reply time was dst 8 (left
  programmed after the sweep), and eth2 only linked at 17.8s.
- **Zero RXDIAG/RXERR console lines** in that run, although frames reached the kernel and replies
  were produced - so the RX path that delivered them did not go through the logged PIE poll path
  (or the interrupt was not taken / the completion logged elsewhere).  That is the open question
  the RX-stroke stage targets.

## 2. Patch 055 = `luofu_switch.cpu_mirror=1@BT@ (W18) + `luofu_pie.rxstroke=1@BT@ + sweep fix

- **Sweep fix**: candidates are now 0..15; destinations >15 are rejected with an explicit log line
  explaining the 4-bit encoding (ports 40..43 are not valid mirror destinations).
- **W18 static CPU-copy**: luofu_switch.cpu_mirror=1 keeps the eth2/port 11 ingress mirror to
  luofu_switch.mirror_dst (default 8) for the whole run, with the same snapshot/readback/restore
  discipline as W17.  This is the configuration to test for sustained traffic.
- **W18_RXSTROKE** (luofu_pie.rxstroke=1): every 2 s for 60 s logs per RX channel the hardware read
  pointer, software read pointer, IRQ mask/status and RX enable; also logs every RX interrupt entry
  (first 64).  This tells whether completions happen without the interrupt, on another channel, or
  whether the mirror path bypasses the PIE rings entirely.

## 3. Next run matrix (attended)

1. **Stable static**: full W3-W16 chain + `luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=8
   luofu_pie.rxstroke=1 luofu_pie.rxdiag=1 luofu_pie.accepterr=1@BT@; host ping for ~60 s.  Expected:
   RXSTROKE lines every 2 s; if hw_rptr advances during ping but no RXDIAG/poll lines appear, the
   missing link is the IRQ/channel mapping; if hw_rptr does not advance, the frames are not reaching
   the PIE rings through this mirror mode (look for a separate CPU-port queue).
2. If dst 8 is wrong: repeat with `mirror_dst=6@BT@ and `=7@BT@ (the reply window coincided with those
   sweep phases before 8 took over).
3. If static dst 8 sustains traffic, make it the default data-path workaround and move on to
   verifying ping/ssh stability; then the native L2/VLAN/rootif forwarding remains the clean fix.

## 4. Deliverables / validation

- Patch `055-net-lsw-mirror-stable-rxstroke.patch` (`d4f816ff...@BT@): all patches apply (56/56, 0 rejects),
  both drivers compile clean under WERROR; checkpatch 0 errors (2 cosmetic blank-line checks).
- This SUMMARY is the lane record; the docs stack update is included in the next handover.
