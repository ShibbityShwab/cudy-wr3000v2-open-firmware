# wifi-r1-band-diff - why the second AP start's ctx+0x11 (band) byte reads 0

**Lane:** wifi-r1-band-diff (shared task **task-183**), 2026-11-27.
**HOST-ONLY**: this lane read the local driver sources and the committed
firmware decodes only. It never contacted the router, never wrote the device,
never flashed and never fired. **The Lead fires.**

## The result in one paragraph

The R1 fire's stored word `0x01000028` was **not produced by the second
`ap_chan` frame and is not a band-0 channel-worker store**. Its only possible
producer is the **VAP-start frame (g0 id1 key 0x0065)** that every successful
AP start posts after the channel/AP config: firmware worker CA 0x46394 stores
`body[2] -> ctx+0x11` (band) and `body[3] -> ctx+0x12` (field), and the
clean-room provider never copies the band into `body[2]` (its builder fields
`cfg_a/cfg_b` stay 0). So the first start's own VAP start rewrote the
channel worker's correct store `0x01000128` (ch40 band1 field0 index1) into
`0x01000028` (band 0), 1.5 hours before the R1 restart - the Lead's CW-2
read (2026-11-27 12:00) already shows the clobbered word on the live first
AP. The R1 second start then read that stale word (its own `0x0124` re-store
was not observed in 300 ms; the page was already golden:0 before the R1
sequence) and the gate refused correctly on wrong state. The minimal fix is
one body field: `gp.cfg_a = p->band` in `vapscan_vap_start()`.

## Files

| file | what |
| --- | --- |
| `REPORT.md` | the root cause, the hypothesis verdicts, the ranked no-store candidates, the fix, the verification and the fire step |
| `patches/0001-vap-start-band.patch` | the minimal driver patch (one file, one body field) |
| `tools/r1_band_model.py` | the host test: reproduces both ctx words byte-exactly from the decoded store maps, the gates, the controls, and the patched source |
| `tools/build-r1-band.sh` | host build of the patched module from the pinned beacon lane base tree (artifact local evidence only) |
| `tools/run-host-tests.sh` | runs the model against the patched tree |
| `evidence/` | the recorded host-test and build outputs |
