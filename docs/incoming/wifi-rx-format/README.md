# wifi-rx-format - the exact MAC RX ring entry + 36-byte device-header format (and the fail-closed fix)

**Lane:** wifi-rx-format, shared task **task-163**. **Host-only** (this lane
never touches the bench; the Lead fires). **Status:** format decoded and
confirmed against the vendor decoder + stock register dump; two real
divergences from the vendor pre-write contract found and fixed; host model
132/132 checks; patched module builds with 0 warnings (57 params, NOT fired);
read-only bench witness instrument (`dp_rx_probe`) added.

**Write scope:** `opensource/docs/incoming/wifi-rx-format/`. The patched
`.ko` is local evidence only and is never committed.

## The contract in one screen

    ring entry (4 B)      = device VA of buffer+4        (norm 256 x 4, small 512 x 4, cmp 4095 x 4)
    buffer+0x00..0x03     = host word     (vendor: skb pointer; ours: cookie)
    buffer+0x04..0x27     = 36-B device header (zeroed by the HOST before every publish; device writes it)
    buffer+0x28..         = MPDU payload (device)
    header+0x1a u16       = payload length          header+0x20 u32 = next sub-MSDU devVA, 0 = last
    header+0x07 bits7:4   = rx status nibble        header+0x06 u8  = sub-MSDU count
    header+0x12 u16       = copy of the frame-control word
    header+0x00 u32       = PPDU info descriptor devVA
    packed pointer        = index[14:0] | phase<<15; a full 256/512 fill publishes 0x8000
    config block CA 0x40040000 (band 0): +0x1c buflen 0x00b0063c, +0x20/24/28 ring devVAs,
                            +0x2c/30/34 depths 0x100/0x200/0xfff, +0x38/3c producers 0x8000,
                            +0x40 consumer 0, +0x48 mask 0xfffffcc1
    live bank CA 0x40050000: +0x00 status (RX bit 2), +0x08/+0x0c free rptrs, +0x10 cmp wptr

Full decode + evidence: [REPORT.md](REPORT.md).

## Deliverables

| item | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the exact entry/header/config/write-back contract, our driver conformance table, the two divergences (D1 recycled-header scrub, D2 reset guard), the open-bug analysis and the crash-write analysis |
| [patches/0001-rx-format-hardening.patch](patches/0001-rx-format-hardening.patch) | 2 new files + 4 edited files; applies with `patch -p1` to the final association + mgmt-carrier + watermark tree |
| [src/hi5622_rx_format.h](src/hi5622_rx_format.h) / [.c](src/hi5622_rx_format.c) | portable device-free model of the contract (entry, cookie, header parse, refill budget, vendor-mirror config table) |
| [tools/rx_format_selftest.c](tools/rx_format_selftest.c) | 132 host checks, 0 failures |
| [tools/run-host-tests.sh](tools/run-host-tests.sh) | build + run the host test |
| [tools/build-patched-module.sh](tools/build-patched-module.sh) | regenerate the base tree, apply the patch, build + verify (host-only) |
| [tools/route-rx-probe.sh](tools/route-rx-probe.sh) | ROUTER-SIDE read-only `dp_rx_probe` witness script (the Lead runs it) |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | bench recipe + the verdict decision table |
| [TEST-LADDER.md](TEST-LADDER.md) | host ladder (automated) + bench rungs |
| [BUILD-NOTES.md](BUILD-NOTES.md) | chain, artifact identity, reproduction |

## Artifact (host-built, NOT fired)

| item | value |
| --- | --- |
| chain | final association tree `251d08f0` + mgmt-TX carrier `ab9e18a4` + RX refill watermark `d2c608e7` tree + **this lane's patch** |
| stripped | **356,424 B** sha256 `0eadba355ac914c3ecf09dbe50b31d023ff0c7dbd56c2ba7a977f2cc79d5aacd` |
| unstripped | 1,355,436 B sha256 `9fa4794c98310b40b579e38731af988ca06a847ce9adb842b309b329b7dc9d08` |
| params | **57** (unchanged; no new module parameter) |
| new symbols | `hif_rx_*` portable rules, `dptx_rx_publish_one`, `hi5622_dp_rx_probe_show` |
| new sysfs attr | `dp_rx_probe` (0444, read-only, no register write, no pointer moved) |
| build warnings | 0; .text `136965bb...` (baseline `7403aaff...`) |

The sha is for that exact file; a Linux module build-id hashes the pre-strip
DWARF `comp_dir`, so rebuilds in another directory are code-identical but
carry a different sha - compare rebuilds by .text + params.

## Why this matters for the open bug

1. The ring entry + 36-byte header contract was already implemented correctly;
   the format is not the reason "the MAC RX captures nothing" (mgmt frames use
   the D2H CRX path, not these rings; idle rptr 0x20 / cmp wptr 0 is the
   vendor stock mirror).
2. `dp_rx_probe` makes the next bench run decisive: it says whether the MAC
   ever DMA'd a received frame into host memory (host-word cookie intact +
   nonzero header), independent of the completion pointer path.
3. The only RX write that can fault the MAC is a free-ring producer publish;
   the patch makes it fail closed on an unexplained consumer jump and verifies
   the whole format at `dp_setup` bind time.
