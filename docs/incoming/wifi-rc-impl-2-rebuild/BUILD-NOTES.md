# BUILD-NOTES - where the RC-2 base came from, and how it was re-created

**Lane:** wifi-rc-impl-2-rebuild (host-only), 2026-11-26. Rebuild of the
`../wifi-rc-impl-2` deliverable after the /tmp scratch trees were cleaned.
Nothing here touches the device. The `.ko` is local evidence only.

## 1. What was missing

The RC-2 lane pinned its base as the **IMAGE #3 merged driver source tree**
(`/tmp/wifi-image-3-module`, 97 source files) in
`../wifi-rc-impl-2/evidence/base-pin.txt`. That tree - and its whole
construction chain under /tmp - was scratch and did not survive. Nothing about
the base was committed as a whole tree: only patches, per-lane `src/` copies of
the files each lane edited, and the pinned identities.

## 2. Where the base came from (the reconstruction)

The project's chain builders are committed and are **recursive**: each one
rebuilds its predecessor when the predecessor's tree is absent, all the way
down to `../wifi-dp-air/src` (54 committed source files, the hardware-fired
combined AP+DP module source). Running the *deepest* committed builder is
therefore the canonical stage-1:

    docs/incoming/wifi-dfs-mode-send/tools/build-dfs-mode-send.sh

which rebuilt (each builder asserting its own artifact identity, all PASS):

| step | tree | identity asserted |
| --- | --- | --- |
| 1 | /tmp/wifi-combined-build (dp-air src + ap-start + ap frames) | 242,572 B `44c6cc12...` |
| 2 | + wifi-image-polish | 242,920 B `d145a205...` |
| 3 | + wifi-dp-tuning | 251,924 B `175a4617...` |
| 4 | /tmp/wifi-stability-build/combined (+ hardening) | `b5dfb883...` |
| 5 | /tmp/wifi-allpatch-build (+ rx/beacon/wpa/sta static) | `5b587949...`, 47 params |
| 6 | /tmp/wifi-assoc-build (+ wpa3/ap-mgmt/sta-ops/data-tx) | 56 params, `251d08f0...` |
| 7 | /tmp/wifi-5ghz-ready-build (+ sibling-gpl + band5g) | 57 params, `13366151...` |
| 8 | /tmp/wifi-vapmsg5g-build (+ vap5g order) | 58 params, `13e7bf4c...` |
| 9 | /tmp/beacon5g-build (+ beacon5g gate) | 59 params, `bc1cc0ea...` |
| 10 | /tmp/r1-band-build (+ one-field VAP-start band) | 356,612 B `e8b17f72...` |
| 11 | /tmp/dfsmode-build (+ desc-table 0x00F7 + dfs-mode 0x01F7) | 61 params, `f5408567...` |

(full log: `evidence/source-chain-rebuild.txt`; the stack identities are the
same ones recorded by every intermediate lane.)

Then the **four IMAGE #3 chains** were applied to a clean-stage of the
dfs-mode tree, exactly as recorded in
`../wifi-image-3/build-evidence/module-build/source-chain.txt`:

| step | patch | result |
| --- | --- | --- |
| F2 | `../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch` | 2 files, clean (field B 0x04 -> 0x00) |
| T4 | `../wifi-rx-watermark/patches/0001-rx-refill-watermark.patch` | 3 files, clean (`hi5622_rx_live.h/.c`, `hi5622_dp_txrx.c`) |
| BA | `../wifi-ba-agg/patches/0001-ba-skeleton.patch` | 3 files, Kbuild hunk offset +2 (exactly as recorded) |
| HE | `../wifi-ax-he-caps/patches/0001-he-caps.patch` + `../wifi-ax-he-caps/src/` | cfg80211.c clean; the Kbuild hunk **does not apply** (the F2 Kbuild already carries the desc-table/dfs-mode lines) - exactly as recorded: `hi5622_he_caps.o` was inserted manually after `hi5622_dfs_mode.o` and `hi5622_he_caps.c/.h` copied from the HE lane's `src/` |

Result: **97 files**, manifest
`2d87525e1bab6bbf94486244b55fd1396e2ad691645d40c8469d247ffd766c45`, exactly
the pin in `../wifi-rc-impl-2/evidence/base-pin.txt`.

## 3. Proof that this is the IMAGE #3 tree (not an equivalent one)

The source manifest matches the pin (above), and the base module built from the
reproduced tree - in the canonical IMAGE #3 path, with the IMAGE #3 invocation
(`make -s ... M=/tmp/wifi-image-3-module`) - is **byte-identical to the pinned
IMAGE #3 artifacts**:

| form | rebuilt | pinned artifact |
| --- | --- | --- |
| unstripped | 1,441,520 B `88d65372...` | 1,441,520 B `88d65372...` MATCH |
| activity (`strip --strip-debug`) | 379,092 B `545192df...` | 379,092 B `545192df...` MATCH |
| `.text` | `494ccd42...` | `494ccd42...` MATCH |

(`evidence/image3-base-identity.txt`; pinned artifacts live in
`/home/shibbityshwab/wifi-image-3-module-artifacts/`.) This settles the
"equivalent tree?" question in `../wifi-rc-impl-2/BUILD-NOTES.md` sec 31-34:
**no substitute base is needed and the pin needs no update.**

## 4. The RC-2 build (on the reproduced base)

    cd ../wifi-rc-impl-2
    BASE_TREE=/tmp/wifi-image-3-module ./tools/build-rc2.sh     # canonical WORK=/tmp/rc2-build

Run twice, identical (run2 log byte-length and identity lines identical):

| field | value |
| --- | --- |
| base pin | PASS (9 pinned files + manifest 2d87525e) |
| patch | 71,453 B sha256 `67083c2d31c19d9f8e4e7c0260a8d618d60ec4bf93015421871c116eec2c16bb` |
| applied files | 14/14 hashes match `../wifi-rc-impl-2/evidence/patch-apply.txt` |
| module (unstripped) | **1,519,716 B** sha256 `ca22773a861db31b524edd69f0c5522b03862207191b1d8b83ba302fc0c97d8b` |
| .text sha256 | `ba81e4b38b4a965143b7a7689c9639525e9af9852e05c968c9da65ff42ab7839` |
| activity form | **401,884 B** sha256 `3d43a3cd261d28245e47a964161a0d605caae97a70baa81bea1eee630e294536` |
| params / vermagic | 67 / `6.18.60 SMP mod_unload ARMv7 p2v8`, depends cfg80211 |
| warnings/errors | 0 / 0 |
| host suites | frame KAT **37/37**, C2 order **25/25**, matrix **76/76** |

The activity form is the expected artifact and the form the lane deploys
(`3d43a3cd...`). Copies: `/tmp/rc2-rebuilt/hi5622_wifi.ko` (activity) and
`/tmp/rc2-rebuilt/hi5622_wifi.unstripped.ko`; plus a gitignored copy in
`build/wifi-rc-impl-2-rebuild/`.

## 5. Fire recipe delta: NONE

The rebuilt module is byte-identical to the recorded RC-2 artifact (activity
sha `3d43a3cd...`, unstripped sha `ca22773a...`, .text `ba81e4b3...`).
`../wifi-rc-impl-2/FIRE-RECIPE.md` therefore stands unchanged - same loader
warning (the IMAGE #3 payload loader writes `h2d_ctx_create=3`; the recipe
requires the manual type-1 ladder) and the same deployment slot. This lane
fired nothing; the Lead fires.

## 6. Reproduce

    ./tools/reproduce.sh                      # full rebuild + all identity asserts
    ./tools/reproduce.sh --with-image3-check  # + the byte-exact IMAGE #3 base check

It needs the project 6.18.60 KDIR + OpenWrt GCC 14.4 toolchain (the same ones
the lane builders discover) and `patch`, `git`, `python3`. ~10 minutes.

## 7. Honesty / limits

* The reproduction is a *reconstruction from committed patches*; it is exact
  because every builder asserts its artifact and the final IMAGE #3 module
  reproduces byte-for-byte. If a committed lane patch or source is edited, the
  chain fails closed - it will not silently build a different base.
* The IMAGE #3 base-module check is path-sensitive (debug info embeds the build
  path); the source manifest is path-independent and is the portable pin.
* No bench contact, no image, no flash, no vendor binary committed here.
