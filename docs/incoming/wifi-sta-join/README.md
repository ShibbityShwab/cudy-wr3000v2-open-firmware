# wifi-sta-join - STA join fire-path hardening + WAL/D2H verification

**Lane:** `wifi-sta-join` (shared task **task-146**), host-only.
**Written:** 2026-11-24. **Parent:** `wifi-sta-static` (task-141).
**Status:** hardened patch + host tests + full compile-verify **PASS**;
bench ladder **designed, not fired** (the Lead fires). No vendor binary,
firmware byte or calibration value in this directory; the pinned vendor
artifacts are local only.

## Why

The sta-static lane delivered the STA connect model (WAL cfg-event opcode
0x0123, 108-byte body) but explicitly left the firmware **dereference
contract of the four host-DRAM body addresses as [U]**, allowed repeated
posts, and had no D2H completion decode. This lane hardens that path:
re-derives the body pointer fields store-by-store, refuses any pointer that
is not a proven device-visible host-DRAM address, adds a dry-run and a
single-shot post with no-retry gates plus a firmware mask/staging health
stop, guarantees no key logging, and decodes the D2H WAL g2 id3/id4
completion family (log-only).

## Deliverables

| path | content |
| --- | --- |
| [REPORT.md](REPORT.md) | full analysis: 108-B body pointer re-derivation + VMSPLIT correction, gates, WAL layout, D2H completion decode, corrections, open [U] |
| [TABLES.md](TABLES.md) | T1-T13: anchors, envelope, body map, address policy, gate matrix, mask/staging witnesses, D2H dispatch + payload maps, artifact identity |
| [TEST-LADDER.md](TEST-LADDER.md) | topology (external AP: phone hotspot `slowphone` or second AP), host rungs H0-H3, bench rungs B0-B4 + stop rules |
| [patches/0001-sta-join-hardening.patch](patches/0001-sta-join-hardening.patch) | applies on the FINAL ALL-PATCH module tree (opensource `008aca5` all-patch integration), 8 files |
| [src/](src/) | hardened portable/kernel sources (logic, frames, STA path) |
| [tools/fw_sta_join_re.py](tools/fw_sta_join_re.py) | 86-check static re-derivation from the pinned local vendor module |
| [tools/sta_join_selftest.c](tools/sta_join_selftest.c) | 111 host checks (bounds, gates, body/frame map, D2H decode) |
| [tools/audit_no_key_log.py](tools/audit_no_key_log.py) | 23-check safety audit (no key/hexdump logging, fail-closed gates, no vendor binaries) |
| [tools/selftest.sh](tools/selftest.sh) | runs all host checks |
| [tools/build-sta-join-hardening.sh](tools/build-sta-join-hardening.sh) | applies the patch on the all-patch tree, zero-warning compile, strip/identity assertions, selftests |
| [evidence/](evidence/) | reproduction output (build, artifact identity, selftests, re-derivation) |

## Headline results

1. The 108-B body carries **four raw cfg80211 pointers** the firmware
   dereferences. The vendor build only works because it runs **VMSPLIT_2G**
   (kernel VA == device host-DRAM address); our **VMSPLIT_3G** kernel must
   pass `dma_alloc_coherent` **device addresses** in the bench-proven
   `0x80000000..0x8fffffff` window. The hardened builder enforces
   non-zero/aligned/in-window/in-buffer/fixed-layout/no-wrap on all four,
   re-parses the finished body, and keeps the firmware contract [U] behind a
   one-shot gated post.
2. Fire path: `sta_join=0` fail closed (default), `1` dry run (real frame
   built+validated including the DMA address math, **no key material
   copied**, nothing posted), `2` **one post per module load**, requires
   `allow_frames>0`, **no retry** on any failure, **stops on mask/staging**
   (firmware CPU 0x400008 / 0x40000c, already latched by the HCC exception
   watch).
3. D2H completion decoded: WAL chip-group-2 dispatch table (19 ids;
   **id3 = wal_asoc_comp_proc_sta -> cfg80211_connect_done**, **id4 =
   wal_disasoc_comp_proc_sta -> cfg80211_disconnected**), payload map
   (status +0x02, bssid +0x04, IE lengths +0x0c/+0x10, **firmware-supplied
   IE addresses +0x14/+0x18 = untrusted**), wired log-only into the direct
   D2H reader. Never dereferences the firmware addresses; never fakes
   `cfg80211_connect_done` (RX/AP-mgmt lanes own completion wiring).
4. No key logging: static audit PASS.
5. Compile-verify on the all-patch tree: **zero warnings**, debug-stripped
   **314,244 B sha256 `e4e61184...`**, 47 params (unchanged), host tests
   111 + re-derivation 86 + audit 23 checks PASS.

## Quick reproduction

```sh
# no device, no vendor bytes committed:
./tools/selftest.sh                       # H0 + H1 (if local vendor ko) + H2 audit

# full compile verification (needs the project 6.18.60 tree + staging dir):
./tools/build-sta-join-hardening.sh       # uses /tmp/wifi-allpatch-build/integ
INTEG_TREE=/path/to/integ ./tools/build-sta-join-hardening.sh

# static re-derivation only (pinned local vendor module, never committed):
.provision-venv/bin/python tools/fw_sta_join_re.py /path/to/hi5622v100_wifi.ko
```

Bench: [TEST-LADDER.md](TEST-LADDER.md). This lane does not fire.
