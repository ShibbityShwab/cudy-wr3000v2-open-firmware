# Phase 3 verification — analysis files + published-repo audit

- Date: 2026-09-30
- Device: `root@192.168.10.1` (WR3000, kernel 5.10.201) — read-only; every probe created was removed again.
- Local checkout: `/c/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/opensource`
- Remote: `https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware.git`
- Repo HEAD (local == remote `master`): `3157cf96772d724153de5120630b32b9f0bd4d43`

Device commands below were run as
`SSH_ASKPASS=... ssh <opts> root@192.168.10.1 "<inner command>"`; the `<inner command>` is shown verbatim.

---

## PART 1 — the three phase-3 analysis files

### 1(a) `alg-commands.md`

| # | file | claim | exact command | observed | verdict |
|---|---|---|---|---|---|
| A1 | alg-commands.md | §2 table has 414 rows | `awk '/^## 2\. Full table/,/^## 3\./' alg-commands.md \| grep -cE '^\| [0-9]+ \|'` | `414` | PASS |
| A2 | alg-commands.md | §4 probe table has 184 rows | `awk '/^## 4\. On-device probe/,0' alg-commands.md \| grep -cE '^\| [0-9]+ \| `iwpriv'` | `184` | PASS |
| A3 | alg-commands.md | probe result `142 SUCC / 42 FAIL` | `awk '/^## 4\. On-device probe/,0' alg-commands.md \| grep -E '^\| [0-9]+ \| `iwpriv' \| grep -c SUCC` and `... \| grep -c FAIL` | `SUCC=142`, `FAIL=42` (sum 184, no row is both/neither) | PASS |
| A4 | alg-commands.md | row 1 `get_rate_mode` → `[SUCC][iw]get_rate_mode: auto` | `timeout 3 iwpriv Hisilicon0 alg get_rate_mode` | `Hisilicon0  alg:[SUCC][iw]get_rate_mode:` / `auto` (EXIT=0) | PASS |
| A5 | alg-commands.md | row 8 `get_debug_log_switch` → `[SUCC][iw]debug_log_switch:disable!` | `timeout 3 iwpriv Hisilicon0 alg get_debug_log_switch` | `Hisilicon0  alg:[SUCC][iw]debug_log_switch:disable!` (EXIT=0) | PASS |
| A6 | alg-commands.md | row 20 `get_sch_mode` → `[SUCC]sch_mode = auto` | `timeout 3 iwpriv Hisilicon0 alg get_sch_mode` | `Hisilicon0  alg:[SUCC]sch_mode = auto` (EXIT=0) | PASS |
| A7 | alg-commands.md | row 2 `get_fec_coding` → `[FAIL][Error]Invalid CMD input, pkt type[13] invalid` | `timeout 3 iwpriv Hisilicon0 alg get_fec_coding` | `Hisilicon0  alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` (EXIT=0) | PASS |
| A8 | alg-commands.md | row 7 `get_freq_bw_mode` → `[FAIL]` | `timeout 3 iwpriv Hisilicon0 alg get_freq_bw_mode` | `Hisilicon0  alg:[FAIL]` (EXIT=0) | PASS |

Note: the file's probes were issued against `vap0`; the re-run used `Hisilicon0` as instructed by the task. Same `alg` private ioctl, same firmware response — only the netdev prefix differs. Every re-run output equals the file's row after stripping the interface name and collapsing newlines.

### 1(b) `wire-capture.md`

| # | file | claim | exact command | observed | verdict |
|---|---|---|---|---|---|
| B1 | wire-capture.md | a `t`-kprobe on `hmac_sync_dmac_alg_cfg_rsp_entry` can be created | `cd /sys/kernel/debug/tracing; echo "p:verify_alg hmac_sync_dmac_alg_cfg_rsp_entry" >> kprobe_events` | `CREATE_RC=0`; `cat kprobe_events` → `p:kprobes/verify_alg hmac_sync_dmac_alg_cfg_rsp_entry` | PASS |
| B2 | wire-capture.md | `iwpriv Hisilicon0 alg get_2g_power_param` triggers one entry | `echo 1 > events/kprobes/enable; echo 1 > tracing_on; echo > trace; timeout 3 iwpriv Hisilicon0 alg get_2g_power_param; cat trace` | `[SUCC]17161605 17161605 ... 0a0606ff` (identical payload to the file); trace: `entries-in-buffer/entries-written: 1/1` → `Host MSG RX -1170 [001] d... 847.383536: verify_alg: (hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi])` | PASS |
| B3 | wire-capture.md | probe removed, `kprobe_events` empty afterwards | `echo 0 > events/kprobes/enable; echo "-:verify_alg" >> kprobe_events; wc -c < kprobe_events; wc -c < /sys/kernel/debug/kprobes/list` | `DEL_RC=0`; `kprobe_events_bytes=0`; `probe_list_bytes=0`; `tracing_on=0` restored | PASS |

The re-run reproduces the file's §2.3 signature exactly: one entry per request, on kernel thread `Host MSG RX` (pid 1170), CPU 1, flags `d...`, at `hmac_sync_dmac_alg_cfg_rsp_entry+0x0/0x160 [hi5622v100_wifi]`. First attempt produced an empty trace because `events/kprobes/enable` had not been set; after `echo 1 > events/kprobes/enable` the capture succeeded — same mechanism the file documents. Device left clean.

### 1(c) `firmware-forensics.md`

Recomputed with `./pyenv/Scripts/python.exe` against `build/tmp/FIRMWARE.bin`.

| # | file | claim | exact command | observed | verdict |
|---|---|---|---|---|---|
| C0 | firmware-forensics.md | size/sha256/md5 identity | `sha256sum build/tmp/FIRMWARE.bin` (plus hashlib) | `928920` bytes, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`, md5 `0e530b976d5a20e87358671f1a577695` — all equal to §0 | PASS |
| C1 | firmware-forensics.md | header words `0x00046971`, `0x000C742D`; 56 zero bytes | `python -c "import struct;d=open('build/tmp/FIRMWARE.bin','rb').read();print(['%08x'%struct.unpack_from('<I',d,i)[0] for i in range(0,64,4)])"` | `00046971 000c742d` then 14 zero words; raw `71 69 04 00 2d 74 0c 00 00 ...` | PASS |
| C2 | firmware-forensics.md | banner `ChenTangV100R001C20T13` present, once | `python -c "...;print(d.count(b'ChenTangV100R001C20T13'), hex(d.find(...)))"` | `count 1 at 0xcd2ec` (file §4.2 says `0x0cd2ec`) | PASS |
| C3 | firmware-forensics.md | `VERIFY20M` / `VERIFY40M` tokens | `python -c "...;print(d.count(b'VERIFY20M'), hex(d.find(b'VERIFY20M')))"` | `VERIFY` count 2; `VERIFY20M` at `0xc44de`, `VERIFY40M` at `0xc44e8` (matches §4.3) | PASS |
| C4 | firmware-forensics.md | `DEADBEEF` (`ef be ad de`) occurs exactly once | `python -c "...;print(d.count(b'\xef\xbe\xad\xde'), hex(d.find(b'\xef\xbe\xad\xde')))"` | `count 1 at 0xe259c` (matches §5.3) | PASS |
| C5 | firmware-forensics.md | 4 KB entropy max `7.2561` bits/byte | `python -c "import math,collections; ... 4096-byte blocks ..."` | `blocks 227 last 3224; min 0.6456 max 7.2561 mean 6.8134` | PASS |
| C6 | firmware-forensics.md | printable runs ≥ 8 = 414, unique 391 | `python -c "import re;r=re.findall(rb'[\x20-\x7e]{8,}',d);print(len(r),len(set(r)))"` | `414 391` | PASS |
| C7 | firmware-forensics.md | `itcm_src + itcm_len == dtcm_src` | `python -c "print(0xf009c+0xadd0==0xfae6c)"` | `True` (matches §2.3) | PASS |

**Part 1 OVERALL: PASS** — all three files' quantitative claims and sampled probe rows reproduce.

---

## PART 2 — published repo audit (`ShibbityShwab/cudy-wr3000v2-open-firmware`)

Local checkout at `/c/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/opensource`, working tree clean, `## master...origin/master` (in sync).

| # | claim | exact command | observed | verdict |
|---|---|---|---|---|
| P1 | remote file list == local committed tree (no stray/missing files) | `git ls-tree -r --name-only HEAD \| sort` vs `curl -s "https://api.github.com/repos/ShibbityShwab/cudy-wr3000v2-open-firmware/git/trees/HEAD?recursive=1"` (blob paths) | `IDENTICAL: 18 files each side`; remote `"truncated": false` | PASS |
| P2 | no credentials — `RouterRoot` (case-insensitive) | `grep -rniI --exclude-dir=.git -- 'RouterRoot' .` and `git grep -inI 'RouterRoot' $(git rev-list --all)` | working tree **0 hits**; all history **0 hits** (also searched the literal device root password from the task recipe, redacted here as `RouterRoot-****`, → 0 hits, and the substring `9x` → 0 hits) | PASS |
| P3 | no credentials — `Wr3000` (case-insensitive) | `grep -rniI --exclude-dir=.git -- 'Wr3000' .` | **19 hits, all non-secret**: model-name/product references in `docs/DRIVER-BLACKBOX.md` (lines 1, 45, 196), `docs/FLASH-PLAN.md` (1, 32, 39, 70, 124, 130), `docs/phase3/wire-capture.md` (3, 22), `docs/systime-rce-writeup.md` (1, 7), `NOTICE.md` (6), `README.md` (1, 3, 4), and the literal path `/tmp/askpass-wr3000.sh` in `tools/wifi-cal-restore.sh:8` and `tools/wifi-cal-snapshot.sh:9`. No password, key, or token. | PASS |
| P4 | no credentials — `omo_key` (case-insensitive) | `grep -rniI --exclude-dir=.git -- 'omo_key' .` and `git grep -inI 'omo_key' $(git rev-list --all)` | working tree **0 hits**; all history **0 hits** | PASS |
| P5 | no credentials — `BEGIN OPENSSH` (case-insensitive) | `grep -rniI --exclude-dir=.git -- 'BEGIN OPENSSH' .` and `git grep -inI 'BEGIN OPENSSH' $(git rev-list --all)` | working tree **0 hits**; all history **0 hits** (no private keys committed) | PASS |
| P6 | no credentials — `gho_` (case-insensitive) | `grep -rniI --exclude-dir=.git -- 'gho_' .` and `git grep -inI 'gho_' $(git rev-list --all)` | working tree **0 hits**; all history **0 hits** (no GitHub PAT) | PASS |
| P7 | no credentials — `password=` (case-insensitive) | `grep -rniI --exclude-dir=.git -- 'password=' .` and `git grep -inI 'password=' $(git rev-list --all)` | working tree **2 hits, both non-secret env-var references**: `tools/web-login.sh:17` → `--data-urlencode "luci_password=$H2"` (H2 is a runtime SHA-256 computed from `$WEB_PASSWORD`, itself required via `: "${WEB_PASSWORD:?...}"`; no literal), and `tools/wifi-cal-restore.sh:13` → usage text `echo "usage: ROUTER_PASSWORD=... ..."` (placeholder). History: same 2 lines × 4 revisions, nothing else. | PASS |
| P8 | no vendor binaries — every committed file is text and < 100 KB | `git ls-files -z \| while read -r -d '' f; do wc -c < "$f"; file -b --mime-type "$f"; done`; plus `git rev-list --objects --all \| git cat-file --batch-check` for hidden history blobs | 18/18 files text (`text/plain`, `text/x-script.python`, `text/javascript`, `text/x-shellscript`); largest = `docs/phase3/alg-commands.md` **52,980 bytes** (< 102,400). No blob ≥ 100 KB anywhere in history. **Zero exceptions.** | PASS |
| P9 | `LICENSE`, `NOTICE.md`, `README.md` exist locally and remotely | `for f in LICENSE NOTICE.md README.md; do [ -f "$f" ]; curl -s -o /dev/null -w '%{http_code}' .../contents/$f; done` | local: all present (1070 / 1361 / 3401 bytes); remote API: `HTTP=200` for all three | PASS |
| P10 | repo visibility | `curl -s .../repos/ShibbityShwab/cudy-wr3000v2-open-firmware` | `"private": false` → **PUBLIC**; `default_branch: master`; `pushed_at: 2026-09-30T20:13:57Z` | PASS |
| P11 | HEAD commit sha (local == remote) | `git rev-parse HEAD` vs `curl -s .../git/refs/heads/master` | local `3157cf96772d724153de5120630b32b9f0bd4d43`; remote `3157cf96772d724153de5120630b32b9f0bd4d43` | PASS |

Extra (not requested but relevant to "stray files"): no untracked files in the checkout (`git status --short` empty) and the only local branch is `master` tracking `origin/master`.

Footnote on HEAD movement: at first contact the checkout was at `1c1006d714410ce190aeaaf8cdb7479d9411e124` ("phase3: firmware blob forensics"). The parent session then committed and pushed `3157cf96772d724153de5120630b32b9f0bd4d43` ("docs: fold phase-3 findings into the black-box dossier") during this audit. All PART 2 evidence above is for `3157cf9…`, which is the current local HEAD and the current remote `master`; the git-history secret scans cover every revision (`git rev-list --all`), including `1c1006d`, `6d7739c`, `adc77a3`, `f6c850a`.

**Part 2 OVERALL: PASS** — remote tree matches local (18 files), no credentials found (0 hits for `RouterRoot`, `omo_key`, `BEGIN OPENSSH`, `gho_`; `Wr3000` hits are all product-name references; `password=` hits are env-var references/usage text), no vendor binaries (all text, max 52,980 B), required docs present both sides, repo PUBLIC at HEAD `3157cf96772d724153de5120630b32b9f0bd4d43`.

---

## OVERALL: PASS

All PART 1 claims (414 table rows / 184 probe rows / 142 SUCC-42 FAIL; the sampled device probes; the kprobe round trip and cleanup; the firmware header/banner/VERIFY/DEADBEEF/entropy values) reproduce, and all PART 2 repo-audit checks pass with every hit reported above. No secrets and no vendor binaries are present in the published tree or its history. Device was left clean (`kprobe_events` 0 bytes, kprobe list 0 bytes, `tracing_on=0`).
