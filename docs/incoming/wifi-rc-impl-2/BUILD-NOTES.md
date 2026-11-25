# BUILD-NOTES - wifi-rc-impl-2: exact base, patch chain and build record (host-only)

**Lane:** rc-impl (task-200).  Nothing here touches the device.  The .ko is
local evidence and is never committed.

## Base: the IMAGE #3 merged driver source tree (the current base)

The project's newest driver module is the IMAGE #3 merged 64-parameter module
(`../wifi-image-3/`): the F2 5 GHz chain + the RX-watermark T4 + the BA skeleton
+ the HE caps.  Its source chain is recorded in
`../wifi-image-3/build-evidence/module-build/source-chain.txt`:

    F2 5 GHz chain (r1-band + desc-table + dfs-mode + field-B fix)
      + wifi-rx-watermark/patches/0001-rx-refill-watermark.patch
      + wifi-ba-agg/patches/0001-ba-skeleton.patch
      + wifi-ax-he-caps/patches/0001-he-caps.patch (+ src/hi5622_he_caps.c/.h)
    = 64-param merged module 379,092 B activity sha 545192df...
      358,544 B installed sha a717c86d...   (image-3 artifacts, local)

That source tree is **scratch, not evidence** (the project never commits the
vendor/built artifacts).  This lane pins it in `evidence/base-pin.txt`:

* `source_files 97` (sources only; the .o/.ko build products are excluded);
* each edited file's sha256 (`h2d_send.c`, `h2d_send.h`, `h2d_send_logic.c`,
  `h2d_send_logic.h`, `hcc.c`, `main.c`, `Kbuild`, `hi5622_hcc.h`) plus
  `cfg80211-provider.symvers`;
* a locale-independent source manifest hash:
  `(cd <base tree>; python3 tools/make-patch.py --manifest --base . | sha256sum)`
  = `2d87525e1bab6bbf...` (97 files).

Staging the base: if `/tmp/wifi-image-3-module` is absent, reproduce it per
`../wifi-image-3/build-evidence/module-build/source-chain.txt` (the four patch
chains above), or point `BASE_TREE` at an equivalent tree and update
`evidence/base-pin.txt` with `tools/make-patch.py --manifest`.

## The patch chain

    base tree (97 source files)
      + patches/0001-rate-ctl-2.patch   (71,453 B, sha256 67083c2d...)
    = the patched tree (103 files: 8 edited + 6 new)

`patches/0001-rate-ctl-2.patch` is a normalized `diff -ruN` (timestamps
stripped so the bytes are host-independent).  It is **the committed source of
truth**; `tools/make-patch.py --verify` stages the pinned base, applies it with
`patch -p1`, and checks the 14 result hashes against
`evidence/patch-apply.txt` (the patch sha256 is pinned there too).
`tools/make-patch.py --regen` regenerates it from a base/patched tree pair.

| file | change |
| --- | --- |
| src/hi5622_rate_ctl_logic.h/.c (new) | portable decoded contract: alg_cfg blob builders, g1 id3 parser, fail-closed matrix + the **corrected-C2 precondition** (`rc_precond_check`) |
| src/hi5622_rate_ctl_wire.h/.c (new) | portable byte-exact wire builder (39-B rate_mode SET, 28-B carrier + blob) using the base's `h2d_frames` library; host-testable |
| src/hi5622_rate_ctl.h/.c (new) | kernel glue: 3 params (rate_ctl default 0, rate_ctl_fb=4, rate_ctl_arbitrary=0), 8 WO + 2 RO attrs, the precondition gate, the D2H g1 id3 capture |
| h2d_send.c/.h | the **read-only `hi5622_h2d_ctx_attest()`** (ctx4+0x190/ctx+0x434/ctx[0] via `hi5622_fw_ram_read` only), the `h2d_ctx_attest` RO attr, create-type recording, the pre-TXQ attestation read + its dmesg line |
| h2d_send_logic.c/.h | `struct h2d_live` gains `ctx_create_type`/`ctx_attested`; `h2d_gate_check()` refuses a TXQ frame without a TYPE-1 create + a passing attestation (`ctx_create_required`/`ctx_create_type1_required`/`ctx190_attest_required`) |
| hi5622_hcc.h | the recorded corrected-C2 state fields |
| hcc.c | `hi5622_rate_ctl_rx()` hook in `hi5622_hcc_deliver()` (D2H g1 id3/syn 0x0101) |
| main.c | the `rate_ctl` sysfs group registration |
| Kbuild | `hi5622_rate_ctl.o hi5622_rate_ctl_logic.o hi5622_rate_ctl_wire.o` |

No register write is added by the patch; the attestation is read-only through
the existing BAR0 region-5 window, and the only device effect is the decoded
config-event frame through the base's `hi5622_frame_post()` path (and the
existing h2d ladder messages, whose gates are now stricter).

## Build (tools/build-rc2.sh)

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/.../linux-6.18.60
    CROSS=<staging>/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    make ARCH=arm CROSS_COMPILE=$CROSS -C $KDIR M=<patched tree> modules

The script discovers KDIR/STAGING_DIR (`KDIR_OVERRIDE`/`STAGING_OVERRIDE`
override), fails on any warning/error/conflict marker, and records the artifact
identity in `evidence/build-module.txt`.

Recorded result (2026-11-28, two consecutive builds identical):

| field | value |
| --- | --- |
| module (unstripped) | 1,519,716 B sha256 `ca22773a861db31b...` |
| .text sha256 (stable identity) | `ba81e4b38b4a965143b7a7689c9639525e9af9852e05c968c9da65ff42ab7839` |
| activity form (`strip --strip-debug`) | 401,884 B sha256 `3d43a3cd261d2824...` |
| params | 67 (the base 64 + `rate_ctl`, `rate_ctl_fb`, `rate_ctl_arbitrary`) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends / license | `cfg80211` / GPL |
| warnings / errors | 0 / 0 (0 conflict markers) |

The base module's identity before the patch (image-3 activity form) is
379,092 B sha `545192df...`; RC-2 adds 22,792 B to the activity form (the new
surfaces + the attestation + the stricter gate), same vermagic/params surface
plus the three RC parameters.

Deployment: copy the **activity** form over the same slot the h2d-send ladder
uses (`/lib/modules/HI_RELEASE/hi5622_wifi.ko`), then follow `FIRE-RECIPE.md`.
An image bake uses the image lane's `scripts/strip-kmod.sh` (NO_RENAME=1)
transformation; this lane does not produce the installed form.

## Reproduce from scratch (host only)

    BASE_TREE=/tmp/wifi-image-3-module ./tools/build-rc2.sh
    # or step by step:
    python3 tools/make-patch.py --verify
    ./tools/run-host-tests.sh
    ./tools/build-rc2.sh

## Honesty / limits

* The patch does **not** change the decoded wire contract or the carrier; the
  rate SET is the RC-1 decoded command (cfg 401, value 0, 39-B frame).  **This
  lane does not fire**; the bench discriminators are in FIRE-RECIPE.md (the
  corrected C2 is itself the top discriminator).
* The attestation addresses are the bench-pinned ones (ctx4 CA 0x11C458 = boot
  table 0x119758 + 4*0xB40; BAR0 0x7D45E8 / 0x7D4458 / 0x7D488C).  They are
  boot-image constants like every other CA in the project; if a future firmware
  image moves the module contexts, the attestation must be re-pinned (the
  attribute prints the raw values, so a drift is visible).
* The IMAGE #3 payload loader writes `h2d_ctx_create=3` - see REPORT.md sec 2:
  any image carrying this lane must pass 1.
