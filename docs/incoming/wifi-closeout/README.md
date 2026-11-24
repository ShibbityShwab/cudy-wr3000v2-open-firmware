# wifi-closeout - project close-out documentation (task-131)

**Lane:** wifi-closeout (shared task **task-131**), **host-only**, final state **2026-11-27
12:30**. **Scope:** this directory only. The parent repo's `STATUS.md` / `mem-entries.md` and
the opensource docs owned by other lanes (`ROADMAP-OPENWRT.md`, `PUBLISH-READINESS.md`,
`upstream/SUBMISSION-STATUS.md`, the re-report index) are **not** edited here; this lane
delivers the content and the owners paste/merge it.

## What is in here

| file | what it is | consumer |
| --- | --- | --- |
| [LANE-INDEX.md](LANE-INDEX.md) | the complete map of every Wi-Fi and platform lane: task, deliverable, commit, status ([V*]/[V]/[I]/[U]) and evidence - including the final 06:30-12:30 milestones | everyone; the close-out map |
| [EVIDENCE-REFRESH.md](EVIDENCE-REFRESH.md) | verifies the landed report v4 block **EV-001..EV-277** is gap-free, then delivers the **residual rows EV-278..EV-281** (combined AP+DP fire, self-contained final image, close-out deliverables, host-verifier suite), the rows to append to the root `EVIDENCE-INDEX.md`, and the v3/v4 corrections | re-report4 (report index) and the Lead (root index) |
| [OVERVIEW-README.md](OVERVIEW-README.md) | **proposed replacement for `opensource/README.md`**: what this repository is, what is hardware-validated (through the flashed AP and the self-contained final image), how to reproduce (host-only + bench), honest limits, links, glossary | Lead (root doc) |
| [STATUS-NEXT-ACTION.md](STATUS-NEXT-ACTION.md) | proposed `STATUS.md` sec 3m + replacement sec 4 next-action + a suggested ledger entry (12:30 state) | Lead |
| [PUBLICATION-NOTES.md](PUBLICATION-NOTES.md) | close-out publication state: publish surface NOT clean (history purge + 9 fails), the checks this lane re-ran, required order to a clean publish | Lead / publication track |
| [TEST-MATRIX.md](TEST-MATRIX.md) | host-only selftest/verifier sweep: one row per lane verifier, exact command, pass count, classification of every non-PASS entry | everyone |
| [tools/run-host-suite.sh](tools/run-host-suite.sh) | the reproducible runner behind TEST-MATRIX.md (53 entries; `--list`, `--fast`, `--external`, `--only`) | maintainers |
| [tools/run-release-line-check.sh](tools/run-release-line-check.sh) | external-prerequisite wrapper for the upstream release-line validator | maintainers |
| [tools/verify-closeout.py](tools/verify-closeout.py) | lane verifier: deliverables, relative links, EV numbering/collision, no vendor bytes, final-state claims, suite result, lane coverage (+ `--self-test`) | CI/maintainers |
| [build-evidence/host-suite/](build-evidence/host-suite/) | the recorded run: per-entry logs, `summary.tsv`, `RESULT.txt` (text only) | evidence |

## How to verify this lane (host-only, no device, no network)

```sh
cd opensource/docs/incoming/wifi-closeout
./tools/verify-closeout.py            # lane contract: links, EV block, claims, no binaries
./tools/run-host-suite.sh             # re-run every lane selftest/verifier (~4-5 min)
./tools/verify-closeout.py --self-test
```

Expected at close-out: the lane verifier reports **0 FAIL**; the suite reports its totals in
`build-evidence/host-suite/RESULT.txt`, and [TEST-MATRIX.md](TEST-MATRIX.md) explains every
non-PASS entry. Those are not regressions of the delivered artifacts: two are a stale
artifact-pin in the bench-session package (its own selftest correctly fails on it; re-pin
before firing), one is a kernel-tree `Module.symvers` drift in the cfg80211 lane's
provider-symbols baseline, and one is an external-prerequisite SKIP (the release-line validator
needs the pristine `linux-6.18.60.tar.xz`; `--external` runs it).

## Final state this lane documents (one paragraph)

Our clean-room stack boots the Hi5622V100 firmware to the golden state through our own
PCIe/ETE transport and HCC duplex, runs the six-message calibration, registers cfg80211 and
receives real firmware scan results decoded into cfg80211 BSS entries (12 in the first full
decode), and - on the **flashed ABI-correct image** with native kmods/iw/hostapd - brings up
**the first live Wi-Fi AP on our stack** (`wlan0: AP-ENABLED`, SSID `cudy-ap-test`), which
stays live and changes the firmware D2H event stream after start. The MAC data plane is set up
and the first frame is published to the TX ring (POST-ONLY on both the data-plane module and
the combined AP+DP module; the MAC consumer/start is open). A **self-contained final image**
(combined driver + hostapd + ucode/wifi-scripts, kernel slice unchanged) is built host-only and
is the next flash target. The Wi-Fi path needs the **PD4-patched research firmware** (vendor
mailbox dispatch is not host-programmable; the stub is consume-only), so there is **no
upstreamable Wi-Fi driver**; the vendor GPL source request is unanswered; no on-air
beacon/association/WPA2 proof exists yet. The platform line (6.18 kernel + SMP + PCIe +
LSW/PIE/thermal + FIP trailer/image tooling + review-form series 001-026) is the separate
upstream candidate.

## Hard rules this lane followed

- Host-only: no device, UART, TFTP, flash or bench-router contact from this lane.
- No vendor bytes: every delivered file is Markdown/shell/Python text; only sizes, sha256
  hashes, register addresses and already-published frame field values appear.
- No edits outside this directory; concurrent lanes (re-report4, submit-final, img-integ,
  ap-start follow-up) own their scopes and their in-progress files were left untouched (the
  suite only ran their read-only verifiers).
- Claims are marked [V*] (hardware-validated, ledger-dated), [V] (host-verified committed
  artifact), [I]/[U]; a row never upgrades a claim.

## Open items after this close-out

1. Merge the residual EV rows (EVIDENCE-REFRESH sec 1, **EV-278..EV-281**) into
   `wifi-re-report/EVIDENCE-INDEX.md` (owner: re-report4; v4 already landed at EV-277) and
   append the root-index rows (owner: Lead).
2. Paste the `STATUS.md` sec 3m / sec 4 text and the ledger entry (owner: Lead).
3. Adopt `OVERVIEW-README.md` as `opensource/README.md` (owner: Lead).
4. **Flash + validate the self-contained final image** (attended): container ae5e28bf / rootfs
   UBI ff9f3e1c, kernel unchanged; validate `modprobe` + native iw + hostapd AP-ENABLED with
   NO /tmp deployment (owner: Lead, bench session).
5. **External-observer AP breadth** (beacon on air, association, WPA2, DHCP) and **MAC TX
   completion/RX** (vendor TX-engine start/config [U]) - the two remaining bench questions.
6. Publication: generate the v2 review form (30 + DTS-0001; v1 stale) and land the publication
   fixes + history purge ([PUBLICATION-NOTES.md](PUBLICATION-NOTES.md)).
7. Re-pin the bench-session package artifact hashes (`artifacts.env`: merged module
   ec74fccd -> ca32aec4/combined 44c6cc12; AP module 11511980 -> fd8e1439) before it is fired;
   regenerate the cfg80211 provider-symbols baseline against the current tree.
