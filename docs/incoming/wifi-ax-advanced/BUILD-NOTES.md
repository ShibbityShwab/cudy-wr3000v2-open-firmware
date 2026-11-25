# BUILD-NOTES - the portable AX event model and how an implementation lane uses it

**Lane:** wifi-ax-advanced (task-195), host-only.  This lane does **not**
build a module and does not touch the kernel tree: it delivers the decoded
protocol model + host tests + the fire recipe.  The integration is a separate
implementation lane (the pattern of ../wifi-dfs-mode-send and
../wifi-desc-table-send).

## Files and compile

    src/hi5622_ax_events.h      portable header (kernel/host shim)
    src/hi5622_ax_events.c      the nine AX config-event builders + ALG body
    src/ax_events_selftest.c    host selftest (31 checks, exact frame bytes)
    tools/run-host-tests.sh     cc -std=c99 -Wall -Wextra -Werror, no device

The same pair of files compiles in a kernel context (`__KERNEL__` ->
<linux/types.h>/<linux/errno.h>/<linux/string.h>) and on the host
(stdint/stdbool/string/errno shim).  The host build is the contract:
31/31 PASS; the RE tool cross-checks the same frames from the pinned
artifacts (221/221 PASS).

## Integration contract (for the implementation lane)

1. Copy `hi5622_ax_events.{c,h}` into the bench driver tree (as the existing
   lanes copy their portable models).
2. Wrap the posting in a **default-off** parameter and an explicit sysfs
   mode, e.g. `ax_events=off` (byte-identical behaviour to today's module:
   no event, no AX state touched).  Never post at module load: the frames
   target the per-module context and the VAP object that only exist after
   the AP-start ladder (vap_add -> ... -> vap_start).
3. Post from the AP-start completion path with **field B = the AP-start
   module index** (0 in the fired 2.4 GHz ladder - ../wifi-fw-dfs-worker
   found that the wrong field B silently targets the wrong band/context).
4. Order for the first fire (FIRE-RECIPE R2): TWT enable -> ext TWT IE ->
   MU EDCA selector -> BFEE -> SR enable, each step logged, each followed by
   a userspace read-back (add the decoded VAP/ctx addresses as debugfs or a
   small read-only sysfs), and the page-golden check between steps.
5. The ALG text frame (`hi5622_ax_alg`) must stay behind a stricter gate:
   only the unknown-module "no handler" probe is white-box safe until the
   handler addresses from FIRE-RECIPE R0 are decoded.  A valid `sch ...`
   body can change scheduler state; do not post one blind.

## What this lane does NOT claim

* No module artifact is produced here; no artifact contract, no sha, no
  parameter count.  The integration lane's BUILD-NOTES carry that.
* No on-air effect is claimed (the firmware HE-IE/beacon builder is [U]).
* The host-mirror offsets in the vendor builders are host memory, not device
  addresses; only the firmware VAP/ctx offsets in ../TABLES.md T3 are probe
  targets.
* The token grammar below the module name is the firmware handler's business
  and remains [U] until the R0 handler decode.
