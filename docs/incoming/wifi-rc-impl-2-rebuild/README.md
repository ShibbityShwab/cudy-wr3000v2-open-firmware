# wifi-rc-impl-2-rebuild - the RC-2 module rebuilt from committed artifacts

**Lane:** reboot of the rc-impl-2 deliverable after the /tmp scratch trees were
cleaned. **HOST-ONLY**: no device, no UART, no flash, no vendor binary in this
directory. The rebuilt `.ko` is local evidence only (never committed).

## The result in one paragraph

Every `/tmp` source tree of the chain was gone (the IMAGE #3 merged tree
`/tmp/wifi-image-3-module`, the F2 tree `/tmp/dfsmode-build`, and the whole
builder chain below it). This lane re-created the base from the **committed
patches only**, by running the pinned recursive builders up to the dfs-mode
tree and then applying the four IMAGE #3 chains exactly as recorded in
`../wifi-image-3/build-evidence/module-build/source-chain.txt`. The
reproduced tree is **byte-exact**, not merely equivalent:

    source manifest      97 files, sha256 2d87525e...  (= ../wifi-rc-impl-2/evidence/base-pin.txt)
    image-3 base module  unstripped 1,441,520 B 88d65372...  (= pinned artifact)  MATCH
                         activity     379,092 B 545192df...  (= pinned artifact)  MATCH
                         .text                     494ccd42...  (= pinned .text)   MATCH

On that tree the lane re-ran `../wifi-rc-impl-2/tools/build-rc2.sh` (the RC-2
patch, sha256 `67083c2d...`, applied with 14/14 file hashes verified). The
rebuilt module is **byte-identical to the recorded RC-2 artifact**, twice:

    module (unstripped)  1,519,716 B  sha256 ca22773a...   .text ba81e4b3...   67 params
    activity form          401,884 B  sha256 3d43a3cd...   <- the expected artifact
    host suites          frame KAT 37/37, C2 order 25/25, matrix 76/76

Nothing was lost vs the intended IMAGE #3 base: the source tree is the same
tree, and the module hash is the pinned hash. **No pin update is needed**
(`../wifi-rc-impl-2/evidence/base-pin.txt` already matches), and **no fire
recipe delta is needed** (the module is byte-identical, so `../wifi-rc-impl-2/
FIRE-RECIPE.md` stands unchanged).

## Deliverable

    /tmp/rc2-rebuilt/hi5622_wifi.ko             the rebuilt ACTIVITY form (401,884 B, sha 3d43a3cd...)
    /tmp/rc2-rebuilt/hi5622_wifi.unstripped.ko  the unstripped form (1,519,716 B, sha ca22773a...)
    build/wifi-rc-impl-2-rebuild/               gitignored copy of both (repo build/ area)

## Files

| file | what |
| --- | --- |
| `BUILD-NOTES.md` | where the base came from, the exact chain, patch record, byte-exact proofs, build record |
| `tools/reproduce.sh` | full reproduction (chain -> image-3 tree -> rc2 build -> identity asserts) |
| `evidence/source-chain-rebuild.txt` | the chain rebuild log with every builder's identity assertion |
| `evidence/image3-base-identity.txt` | the reproduced tree + its byte-exact image-3 base module |
| `evidence/rc2-build.txt` | the `build-rc2.sh` log (verify PASS, tests, artifact identity) |
| `evidence/rc2-build-runs.txt` | two runs, identical |
| `evidence/host-tests.txt` | frame KAT 37/37, C2 order 25/25, matrix 76/76 |
| `evidence/artifact-copy.txt` | the local artifact copies |

## Reproduce

    ./tools/reproduce.sh                  # chain + image-3 tree + rc2 build + asserts (~10 min)
    ./tools/reproduce.sh --with-image3-check   # + byte-exact IMAGE #3 base-module rebuild (~+1 min)

## Honesty / limits

* Host-only; nothing here was fired or read from a device. The fire belongs to
  the Lead and follows `../wifi-rc-impl-2/FIRE-RECIPE.md` **unchanged**.
* The reconstruction is faithful only because the committed builder chain
  asserts every intermediate artifact (size+sha). If a lane patch is edited,
  the chain fails closed instead of silently substituting a base.
* The `.ko` is path-sensitive through debug info: the byte-exact base-module
  check needs the canonical `/tmp/wifi-image-3-module` path (the source
  manifest is path-independent). The RC-2 artifact was built by the lane's own
  script in its canonical `/tmp/rc2-build` path, which is why its sha matches.
