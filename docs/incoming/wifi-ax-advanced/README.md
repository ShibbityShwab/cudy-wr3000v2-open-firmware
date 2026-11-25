# wifi-ax-advanced - the vendor 802.11ax advanced machinery (OFDMA / MU-MIMO / TWT / BSS color)

**Lane:** `wifi-ax-advanced` (shared task **task-195**). **HOST-ONLY**: this
lane read the pinned local vendor firmware image and host module in place; it
never contacted the router, never flashed and never fired. **The Lead fires.**

**Why this lane exists.** The 2.4 GHz HE AP is live (the HE Capabilities + HE
Operation IEs are on air, /tmp ledger 2026-11-27: params `0x003FF4`, BSS
Color 25) but the *advanced* Wi-Fi 6 machinery is still a black box: no
OFDMA trigger/RU scheduling, no MU-MIMO sounding, no TWT agreement and no
BSS-color control exists in our clean-room driver.  This lane decodes what
the vendor host actually sends for those features, which firmware worker
consumes it and which state byte it writes - and states honestly what can be
evaluated on the working 2.4 GHz HE AP without the RX gate.

Deliverables:

| file | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the two AX control planes, the ten config-event keys, the ALG text plane, the BSS-color finding, the implementation path, the limits |
| [TABLES.md](TABLES.md) | T1-T12: every address, table row, worker instruction, body layout, probe address, frame byte |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the read-only probe pack (R0) and the AX-event rung (R1), the zero-rebuild BSS-color observation (R2), PASS/FAIL and abort rules |
| [tools/fw_ax_advanced_re.py](tools/fw_ax_advanced_re.py) | re-derives every claim from the pinned artifacts (`--selftest` 210/210 PASS) |
| `src/hi5622_ax_events.{c,h}` | the portable host frame model for the nine AX config events + the ALG text body (kernel/host compile the same code, fail-closed) |
| `src/ax_events_selftest.c` | host selftest: 31 checks incl. the exact T12 frame bytes and every refusal path |
| `BUILD-NOTES.md` | the integration contract for the implementation lane |
| `tools/run-host-tests.sh` | the no-device test entry point (C model 31/31 + RE tool 221/221) |
| evidence/ | `selftest.txt`, `keys.txt`, `decode.txt`, `alg.txt`, `host.txt`, `frames.txt`, `probes.txt`, `host-tests.txt` |

Bottom line (details in REPORT.md):

1. **The vendor has TWO AX control planes.** (a) The static g0-id1 config
   events (table A 217 rows / table B 88 rows) carry the small feature
   switches: HE GI `0x006F`, ext-TWT IE `0x00E9`, TWT enable `0x035D`,
   the five MU EDCA parameter selectors `0x005B..0x005F`, BFER `0x04C8`,
   BFEE `0x0335`, trigger SU BF feedback `0x032E`, cyclic-scheme `0x0353`,
   multi-AP SR enable `0x04B6`, multi-AP tool `0x04B4`. (b) The **ALG config
   text plane**: g0-id1 key `0x0128` (table B row 28, worker CA `0x8313C`)
   carries a token-count/offset/NUL-separated text body whose first token is
   a module name - `sch` (the scheduler, i.e. the OFDMA/sounding/RU
   parameters), `cmn`, `err_name`; the handler is a runtime-installed
   callback in the boot hook block (`CBH+0x348/0x34C/0x350`), so its address
   is a read-only probe.  The vendor module carries the whole parameter
   vocabulary (`alg_*ofdma*`, `alg_*sounding*`, `alg_*sr*`), which is why
   the OFDMA/MU-MIMO parameter plane is this text protocol, not config events.
2. **The ten config-event workers are decoded to the byte.**  Every worker
   parses a small fixed body and writes a specific field of the firmware VAP
   object (`*(u32)(ctx+0x190)`) or of the per-module context (`ctx`): TWT
   enable -> `ctx+0xDA` bits 6/7; ext TWT IE -> `VAP+0x3DE`; HE GI ->
   `VAP+0x831`/bit fields; MU EDCA -> `VAP+0x1D4+4*sel+24*ac`; BFER/BFEE/
   trigger-SU-BF-feedback -> `VAP+0x1A4..+0x1A8` and `VAP+0x364..+0x367`;
   multi-AP SR -> `ctx+0xEE` (TABLES T3/T4).
3. **TWT has a clean, testable host builder.** The vendor user path
   (`wal_hipriv_twt_enable`) accepts exactly `{1,3}` and the firmware worker
   maps bit0 -> TWT-enable, bit1 -> TWT-required (`0x035D`, 29-byte g0
   frame).  This is the one AX feature a host-only implementation can post
   and read back on the live AP without a client.
4. **BSS color: the on-air IE is a firmware effect, not a host message.**
   The vendor host builds the HE Operation IE (`mac_set_he_operation_params`:
   `4 | ((u16(MIB+0x1BA)>>6)<<4)` - exactly the observed `0x003FF4` for
   TXOP RTS 1023 - and `mac_set_bss_color_info`: color byte = `obj+0xCC` &
   `0x7F`; the ALG SR param id `0xCE5` sets it) but **no g0-id1 key exists
   for it** and the firmware beacon path never reads the host IE builders:
   the observed Color 25 is firmware-side state.  Changing it needs either a
   firmware worker that reads a host message (not found) or a firmware patch;
   R2 is the zero-rebuild differential that classifies the source.
5. **Not testable on the current AP:** DL/UL OFDMA transmission (needs an
   associated AX client and the RX gate), MU-MIMO sounding (same), and the
   MU EDCA element on air (needs a client or a firmware beacon builder that
   reads the decoded VAP state - [U]).  The lane says so instead of claiming
   a feature.

No vendor binary, firmware byte, calibration value, MAC or secret is stored
in this lane; the only bytes committed are *our own host-built protocol
bytes*.
