# TEST-LADDER - wifi-rx-format (task-163)

Host-only lane. **The Lead fires the bench; this lane does not.** Every bench
rung is read-only and fail-closed, one load per boot, attended with the
recovery door armed. Page golden `0x86807FEF`, host MAC mask (config +0x48)
`0xfffffcc1`, staging 0 on every rung.

Module under test: the RX-format-hardened module (host-built, NOT fired),
stripped **356,424 B** sha256 `0eadba35...`, 57 params (identity in
[BUILD-NOTES.md](BUILD-NOTES.md)).  It is the `d2c608e7` final association +
mgmt-carrier + watermark tree plus [patches/0001-rx-format-hardening.patch](patches/0001-rx-format-hardening.patch).

## 1. Host ladder (automated, this lane; all PASS)

Run `./tools/run-host-tests.sh` - **132 checks, 0 failures**
(log: [evidence/host-test.txt](evidence/host-test.txt)).

| rung | what is proven |
| --- | --- |
| H0 geometry | literal values: entry 4 B, header 36 B at buffer+4, payload buffer+0x28, host word 4 B, norm alloc 0x640/dev 0x63c/256, small alloc 0xb4/dev 0xb0/512, cmp 4095 |
| H1 entry | encode = buf+4, decode round-trip, unaligned/zero refusal, completion-entry pool classification (norm/small/out-of-pool/unaligned) |
| H2 host word | cookie pack/check (kind bit + slot window, range refusals), stale detection, prepare = cookie + zeroed 36-byte header while payload bytes stay untouched |
| H3 header parse | literal offsets for len/next/sub-MSDU/status/fctrl/PPDU; len 0 / over-bound / small-ring bound refusal; aligned vs unaligned chain pointer; the vendor's `len+0x28` bound |
| H4 witness | untouched (cookie + zero header) vs witness (cookie + nonzero header) vs stale (cookie mismatch) - the `dp_rx_probe` decision inputs |
| H5 recycle | the stale-header recycle case: a buffer carrying an old len and an old sub-MSDU chain pointer is scrubbed by the next prepare (D1) |
| H6 reset guard | `posted + delta <= depth` accepted at the boundary, REFUSED for the observed bench shape (posted 224, delta 224), empty-ring full-lap accept, depth/arg refusals (D2) |
| H7 config mirror | the 8 vendor stock-mirror words (+0x1c/+0x2c/+0x30/+0x34/+0x38/+0x3c/+0x40/+0x48) and the "not a constant" case (+0x20) |
| H8 synthetic cycle | 4 buffers: prepare -> encode entries -> device writes header in one buffer -> witness detects it -> parse -> recycle -> witness back to untouched |

## 2. Bench ladder (design only - the Lead fires)

Read-only witness rungs from [FIRE-RECIPE.md](FIRE-RECIPE.md), riding on the
next association fire (`../wifi-assoc-e2e/RUNBOOK.md` order:
`AP -> dp_setup -> dp_txq_sync -> dp_rx_arm`):

| rung | action | PASS means |
| --- | --- | --- |
| F1 | `route-rx-probe.sh --probe` + `--snapshot` on the armed idle AP | rings/config self-checked; `witness=0 untouched=768 stale=0`, `cmp_ring nonzero=0`; page golden, no exception |
| F2 | probe while the client attempts auth/assoc | verdict recorded; `DEVICE-WROTE-FRAMES` = the capture gap is the completion/report path; `MAC-WROTE-NOTHING` = the mgmt traffic never reached host buffers (D2H CRX/RF/firmware path, not format) |
| F3 | probe with an associated data source (EAPOL/ARP/ICMP) | ring pointers may move; a `DEVICE-WROTE-FRAMES` + `cmp_ring nonzero` combination moves the bug into the completion-path lane with hard evidence |
| F4 | every snapshot | page/mask deviation or `excp_announce=1` -> STOP, physical power-cycle, keep the snapshot as finding (B) |

Bounds: a rung never writes a register and never moves a ring pointer; no
reload in the same load; one idea per load.
