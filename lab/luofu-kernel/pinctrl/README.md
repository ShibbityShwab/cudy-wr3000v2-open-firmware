# pinctrl/ - the luofu peripheral IOMUX driver

**Status: RECOVERED.** The driver source exists in this repository, so this directory carries the
source itself, not an `UNRECOVERED` note. It was found at `lab/luofu-pinctrl/` (a sibling of
`lab/luofu-kernel/`, not inside it) and is copied here byte-for-byte behind a 7-line provenance
header.

Nothing in this directory was measured on the device. It is a device-free publication of an
existing artifact: no build, no load, no register read, no device contact.

## 1. Provenance

| Item | Value |
| --- | --- |
| Source path | `opensource/lab/luofu-pinctrl/luofu-pinctrl.c` |
| Repository root | `opensource/` (the git repo root; `lab/luofu-kernel/` is **not** its own repository - `git rev-parse --show-toplevel` from there prints `.../cudy-wr3000v2/opensource`) |
| Last commit touching the file | `eb29735a9b4e59c6b3af99439f0bda93517289d7` (`eb29735`, "luofu: fix the build, enable the pinctrl, and capture the early log in pstore") |
| Implementing commit | `6decd10` ("luofu-pinctrl: implement set_mux from the decoded vendor algorithm"), an ancestor of `master` |
| Body SHA-256 | `7246eaf0745044eaeacd25447c8f987440da9a66037dec04a3338192c4b277b6` (25,528 bytes, 640 lines, LF) |
| Published copy | `pinctrl/luofu-pinctrl.c` = 7-line provenance header + that body |
| Makefile | `lab/luofu-pinctrl/Makefile` copied verbatim, SHA-256 `44e3c5ce8a8130411db5662c7b3214dbbe647dad2c9785b5f0c1d905fb0ebd80` (363 bytes) |

The upstream working copy on this bench differs from the committed blob **only by CRLF**: after
`tr -d '\r'` the two files are identical (`cmp` clean, same SHA-256). The published copy uses the
committed LF bytes, so line numbers below are the upstream line numbers and the published copy is
offset by exactly +7.

**No line in the copied source was changed, and none of its stubs were "fixed".** In particular the
pinconf setters are still no-ops; see section 3.

## 2. What the record claims, and where it says it

| Claim | Where the record states it |
| --- | --- |
| 37 pins / 24 groups / 24 functions transcribed from the vendor module | `~/.omo/memory/.../cudy-wr3000v2/phase52-non-wifi-lanes.md:23` ("tables (37 pins x 5 entries, 24 groups) already transcribed"); `opensource/docs/phase49/README.md:428`; `opensource/docs/UPSTREAM-PORT-PLAN.md:245-246`; `opensource/docs/phase49/gic-view.md:5195-5196` |
| `set_mux` was a no-op stub and was implemented from the decoded vendor algorithm | `phase52-non-wifi-lanes.md:21,23-25` (commit `6decd10`; `luofu-pinctrl.ko` 15,420 B, was 11,536 B) |
| `luofu-pinctrl.c:588` admits a missing `reset_control_deassert` | `~/.omo/memory/.../cudy-wr3000v2/luofu-bringup-lane.md:322,408` |
| The geometry comes from the shipped `hi_kpinctrl.ko` with per-entry offsets | `lab/luofu-pinctrl/README.md:11-19`; `opensource/docs/phase49/gic-view.md:5195-5196`; the source header itself, upstream lines 27-31 and 86-215 |

Two of those sources are **stale on `set_mux`**: `lab/luofu-pinctrl/README.md:25-26` still says
`set_mux` and the pinconf setters are "still no-op stubs", and `docs/UPSTREAM-PORT-PLAN.md:246`
repeats it. Both predate `6decd10`, which changed only the `.c`. They are left as they are; the code
and commit `6decd10` are the authority for the current state.

## 3. What the published source actually contains

Upstream line numbers; add 7 for the published copy.

| Item | Upstream lines | State |
| --- | --- | --- |
| Compatible string `hsan,luofu-peri-pinctrl` | 4, 563-567 | matches the pinned DT node (`docs/soc/luofu-r116-pinned.dts:1271`) |
| "mux" / "cfg" windows | 79-80 (mux base `0x14900100`, size `0x3c`), probe at 578-585 | taken from the pinned DT's `reg`/`reg-names` (`luofu-r116-pinned.dts:1274-1275`) |
| Pin table | 92-103 | **37** `LUOFU_PIN(n)` entries, `pin0`..`pin36` (comment at 86-89) |
| Group table | 155-180 | **24** `LUOFU_GROUP(...)` entries with real per-pin lists (109-153) |
| Function table | 198-205 | **24** names, 1:1 with the groups (195-197) |
| Per-pin mux data | 225 onward | `luofu_pin_mux[37][5]`, entries `{reg, shift, func}` (217-223) |
| `set_mux` | 467-500 | **IMPLEMENTED**: per-pin dword read-modify-write selecting the matching function and clearing the rest (476-489), plus the group mask/value RMW for `jtag_gpio2..5` (491-497). Wired at 506. |
| pinconf setters | 509-551 | **NO-OP STUBS** by design: `luofu_pin_config_set` has a comment naming the vendor algorithm but returns 0 without a write (514-520); `luofu_pin_config_group_set` is `/* NO-OP STUB. */` (533-536); the getters return `-ENOTSUPP` (522-527, 538-544) |
| Reset release | 587-603 | the probe fetches the reset array and calls `reset_control_deassert()` (602). The comment at 587-596 explains this was a TODO and is the same defect that made the MDIO return `-EIO`. The memory note's `:588` refers to an earlier revision; in the current file line 588 is that explanatory comment. |

The source's vendor-evidence citations are all internal and were copied untouched: pin desc array
`.data+0x428`, group table `.data+0x68`, pin-number words `.rodata+0x32c..0x3e4`, function entries
`.rodata+0x20c`, group-name array `.data+0x5e4`, per-pin drv_data `.data+0x644..0xf84`, and
`hi_pinmux_set_mux` @ `.text+0x348` (upstream lines 27-31, 87-90, 110-113, 195-197, 209-215).
No new register value or bit was introduced by this publication.

## 4. Not verified here

- The module was **not** rebuilt or loaded by this task. The record's build evidence is the CI run
  and local build quoted at `phase52-non-wifi-lanes.md:25` and `docs/phase49/README.md:440-448`
  (cross-build green, `vermagic=5.10.201 SMP mod_unload ARMv7`); those artifacts are not
  re-verified here.
- The pinconf behaviour (`pin_config_set`, bias/drive/slew) is unverified: the source says so, and
  this copy adds no evidence.
- Whether the pinctrl block explains the wired-link wall is **not** established. The record's
  position is that it is "the remaining candidate" (`luofu-bringup-lane.md:404-410`), explicitly not
  a measured result.

## 5. Verify

```sh
# the body is byte-identical to the committed source
tail -n +8 pinctrl/luofu-pinctrl.c | sha256sum
# expect 7246eaf0745044eaeacd25447c8f987440da9a66037dec04a3338192c4b277b6

# geometry, counted from the table macros
grep -o 'LUOFU_PIN([0-9]*)' luofu-pinctrl.c | wc -l          # 37
grep -c '^\s*LUOFU_GROUP(' luofu-pinctrl.c                   # 24
sed -n '/luofu_function_names\[\]/,/^};/p' luofu-pinctrl.c \
  | grep -o '"[^"]*"' | wc -l                                # 24

# the stubs, still visible
grep -n 'NO-OP STUB' luofu-pinctrl.c                         # 522, 541 (published; +7 header)
```
