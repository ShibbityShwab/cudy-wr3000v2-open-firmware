# wifi-app-ie - the H2D g0 id 0x20 app/vendor IE injection (task-234)

**Lane:** wifi-app-ie (shared task **task-234**), 2026-11-26. HOST-ONLY: no
bench contact, no device write, no flash. Deliverables live under
`opensource/docs/incoming/wifi-app-ie/` (the shared task's advisory write scope
named `app-ie`; the lane follows the task text's directory name, matching the
sibling lanes).

**The gap.** The first-ever stock H2D capture (sibling lane
wifi-stock-h2d-census, task-232, commit e977557) decoded the vendor's real
families our clean-room stack never sends. One of them is **g0 id 0x20
`hmac_config_h2d_send_app_ie` (builder CA 0x600cc)**: the vendor path that
installs the per-VAP IE table the firmware appends to management frames.

**What this lane delivers.**

| file | content |
| --- | --- |
| `REPORT.md` | the full decode (builder, caller chain, firmware handler), the cfg80211/nl80211 integration design, grades and limits |
| `TABLES.md` | T1-T9: the captured frame, header/payload field maps, handler validation and slot table, slot-class evidence, cfg80211 source mapping, parameter contract |
| `src/hi5622_app_ie.{h,c}` | portable C99 model: frame builder, cfg80211 vendor-IE collector, fail-closed AP-start gate, status line |
| `src/app_ie_selftest.c` | host selftest, incl. a byte-exact reconstruction of the captured stock frame |
| `patches/0001-app-ie.patch` | the kernel integration against the pinned R5-order base (new module + start_ap ladder step + status line + plan step) |
| `tools/run-host-tests.sh` | build + run the host selftest (55 checks) |
| `tools/build-app-ie.sh` | reproducible module build on the pinned base (local evidence only; no .ko committed) |
| `BUILD-NOTES.md` | source/artifact pins and the build proof |
| `FIRE-RECIPE.md` | the attended fire: module load + 2.4 GHz HE AP with hostapd `vendor_elements=` + laptop scan/probe verification + read-only probes |
| `evidence/` | host-test and build evidence (text only) |

**Status:** host-side complete - decode [V] on the wire format and the
firmware handler, the integration built on the pinned base (**0 warnings, 67
params**, artifact pins in BUILD-NOTES.md), host tests **55/55**, and a
read-only firmware slot read-back built into the ladder step. The frame-class
slot semantics remain [I] (TABLES T6); the on-air half is the Lead's attended
fire (FIRE-RECIPE.md). The capture log itself (`/tmp/h2d-stock.log`) is local
evidence, never committed.

**Read order:** REPORT.md -> TABLES.md -> BUILD-NOTES.md -> FIRE-RECIPE.md.
