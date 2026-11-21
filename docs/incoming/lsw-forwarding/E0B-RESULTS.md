# E0-b results: stock vs ours runtime state (2026-11-21)

Both captures were taken read-only with capture-runtime-state.sh --profile punt (9,488 words,
0 read failures each) on a consistent runtime of each side:
- ours: image 6e9c54d0 (adopted baseline), 6.18.60, 2 CPUs, selector 0x11, env bootflag=b.
- stock: vendor 5.10.201 on pair a (bootflag=a, boot_reg=0x10), consistent kernel+rootfs.
Stock was reached by writing env bootflag=a + selector 0x10 from our image and rebooting; the
stock capture was pulled back over the bench LAN; the box was then returned to our image
(env bootflag=b, selector 0x11).

## Evidence (gitignored, reproducible)

- ours: build/evidence-bench-gates/e0b-ours/ (capture.tar.gz + extracted lswcap-*/; regs.txt
  9,488 words)
- stock: build/evidence-bench-gates/e0b-stock/ (capture.tar.gz sha256
  2e2475dbd23eaad7a6ece7bf097146fdb497d2849651152fe2ea6b25e72fa14a; regs.txt sha256
  f1b627674e208bf4d4d843864f33bf7fdb1d39cb4aa4e0621575808a81744a85, 9,488 words)
- diff: diff-captures.py --ours ... --stock ... --out/--plan/--json produced 30 candidates,
  72 counter rows, 1 note.

## Key findings (stock-only state that our init lacks)

1. **tagact entry 1** at 0x124d4010: stock w0=0x00000000 w1=0x0000c000 w2=0 w3=0
   ("vid~0 0x40000=0 mode~0"). Ours: no nonzero tagact entries at all. This is the strongest
   candidate for the CPU-punt/tag classification action.
2. **pfe_glb+0x2c**: stock 0x10000034, ours 0x00000034 - high16 0x1000 matches the previously
   decoded write_glb_attr(0) value; an exact justified write.
3. **EQM block 0x11200048..0x11200070**: stock has queue-manager config (e.g. 0x41100012,
   0x10000000, 0xffff0bb8, 0x02000bb8, 0x07f03fff...), ours mostly zero.
4. **NAPT window 0x12280050..0x12280170**: config words differ / are stock-only; the lane's
   write planner hard-refuses the NAPT window by name (DDR pointer hazards) - do NOT replay these
   without a decoded contract.
5. **L2 window 0x121c0000..**: extensive stock-vs-ours differences (stock 0x414/0x415 patterns vs
   ours 0x148), i.e. the stock CFE/bootloader programmed L2 state that our Linux init never does.
   The L2 window stays read-only/refused in the write planner until decoded; with traffic this is
   where the unicast-to-CPU/bridge-MAC mapping is expected to live.
6. PIE config words (0x11003500..) also differ (stock 0x4/0x141/0xb5 vs ours 0x0/0x3d/...).

## Next (native forwarding M1a)

The minimal first hardware step is a reviewed, fail-closed write of only the justified words:
pfe_glb+0x2c high16 0x1000 and the tagact entry 1 (w0=0, w1=0x0000c000, w2=0, w3=0), then a
host ping with the CPU mirror kept for management, looking for an additional native 98-byte
completion on port 11. Everything in the EQM/NAPT/L2 windows needs a decoded contract before any
replay; the planner already refuses NAPT and L2 by name. The E0-a result (native completions
carry status=37 = exception/punt class) still applies.

### Decode review (2026-11-21, host-side - see E0B-ANALYSIS.md and m1a/)

The follow-up decode changes the reading of three deltas above:

1. tagact entry 1 is a verified tag_write entry (desc[8]=0x08): a tag DELETE action of type 2 with
   no VLAN/TPID (word1 0x0000c000 = selector 4 + the writer's 0x80 bit). It is not a CPU-punt
   classifier action by itself; the tagact_id reference lives in L3-interface/multicast entries
   in the vendor code, none of which are readable/present on our image - so a replay is a
   hypothesis test with an expected null result.
2. pfe_glb+0x2c is decoded by the vendor's own dump_glb_attr as up_port (high16, stock 0x1000 =
   WAN port 12) plus flow_age_en (napt+0x54 bit 8, stock 1). It is a flow-engine attribute, not an
   L2/CPU-delivery control; the flow_age_en half is in the refused NAPT window.
3. The L2 0x414/0x415 words are L2 DFX COUNTERS (rx_total/tx_total and friends at l2+0x40000,
   vendor hi_pfe_dfx_dump_cnt string resolution), not port maps or MAC entries. The L2 page stays
   read-only; the readable L2 config words are identical on both images.

The EQM words are indirect command/data residue, the PIE words are our own driver's ring config,
and IFC shows no stock-only entries. The reviewed minimal probe set (m1a/) is therefore tagact
entry 1 + pfe_glb+0x2c only, with fail-closed scripts and a null-result expectation.
