# incoming/qm-decode - round 4g: read-only LSW state dump (patch 045)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 12).  Prior record: `032`, `036`,
`038`, `040`, `041`, `042`, `043` (W10 IFC), `044` (gate-token fix).  This package
delivers **patch 045 (W11)**: a read-only LSW state-dump stage for the stock-vs-ours diff, plus the
round-4g decode finding.  No device access in this lane.

Apply `shared.patch` from the `opensource` root (it carries the W10 docs, the token fix and the
W11 section), then move this SUMMARY to `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Round-4g hardware result (image 24794d64, corrected W10 token)

The W10 gate ran: "IFC default-classification table: 8 entries, command-engine pre-state 0x0 (busy 0,
error 0), tab128 base 0x50000" -> "programmed and verified: 8 entries (5-word command-engine
descriptors acknowledged, 4-word tab128 entries read back exactly)" -> armed line including the IFC
table -> GEMAC netdevs registered, eth2 1 Gbps/Full, br-lan forwarding, heartbeat HB 48..103 stable
through the traffic test, no Oops.  **Traffic is still dead**: host sniff sees only its own ARP
requests, ping fails, no PIE RX traces, no box frames.  All ten stages W3-W10 are now programmed and
verified on silicon.

## 2. Decode finding: the missing state is the vendor runtime forwarding layer

- The vendor's L2/VLAN/port-forwarding configuration is installed at load time and on bridge/netdev
  events by the `hi_kcfe_lrn_*@BT@ / `hi_kcfe_pub@BT@ / `hi_kcara@BT@ module family (kernel modules
  built for the vendor 5.10 kernel).  These modules **cannot load on our 6.18 kernel**, so on our
  image that state is simply absent - and that is exactly the class of state a stock boot has and
  our armed boot does not.
- The PIE per-port "queue handle" the lead asked about (`g_api_priv@BT@) is a **software** structure in
  the vendor PIE driver (kmalloc(64) keyed by (port<<8)|vport), not a hardware register pair; the
  hardware TX descriptor carries only the port tag, and the LSW-side queue/port configuration is
  what our W5-W7 already program.  So it is not a missing hardware write.
- The remaining candidate hardware areas are the L2 table/config, VLAN membership, and per-port
  forwarding state - all owned by the CFE family above.

## 3. Patch 045 (W11): read-only state dump

- New gate `luofu_switch.dump=1@BT@ (read-only, no hardware writes).  After the normal armed path the
  driver prints a bounded register snapshot (4 words per line):
  `l2+0x0000/0x10000/0x20000@BT@, `ifc+0x0000/0x50000@BT@, `dp+0x0000/0x1000@BT@,
  `eps+0x0000/0x1000/0x0b00@BT@, `pe+0x0000/0x0400@BT@, `eqm+0x0000@BT@, `opp+0x0000@BT@,
  `fam+0x0500@BT@, `ips+0x0000@BT@ (~100 lines).
- The dump is taken **after** W3-W10 so it captures our armed state; nothing is written and the
  heartbeat path is unaffected.

## 4. Attended procedure (stock-vs-ours diff = the next write set)

1. Boot the current series with the full chain **plus `luofu_switch.dump=1@BT@**; capture the
   `lsw-dump ...@BT@ lines.
2. On a stock boot (vendor kernel), read the same window+offset addresses with the existing stock
   readout; window bases: l2 0x12180000, ifc 0x12100000, dp 0x11000000, eps 0x11280000,
   pe 0x11180000, eqm 0x11200000, opp 0x12400000, fam 0x11100000, ips 0x11080000.
3. Diff the two captures: every range where stock is non-zero and ours is zero is a missing init.
   That list becomes the next patch's write set, decoded against the vendor module that owns it
   (kcfe_lrn_l2 for L2/VLAN entries, kcara for port state).
4. Optional second boot of the same image: `luofu_gmac.w9=1 hwinitwrite=1@BT@ (cmdline only) to
   exercise the corrected 1518 frame limit with the heartbeat detector.

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/045-net-lsw-state-dump.patch` | W11 read-only LSW state dump (opt-in, no writes) | `f722ca3db2adbef0c07fccdd50101fbe2e63090aea29880390d7c2fb12dd39e0` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | W10 + token-fix + W11 docs (LSW-SPEC, SKELETON-SMOKE, PIE-DATAPATH, TARGET-DESIGN, README) | see handover |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- **Series apply (current full series + 045 = 46 patches):** 46/46, 0 failed hunks, 0 .rej/.orig;
  `luofu-switch.c` byte-identical to the lane tree (`logs/10-series-apply-046b.log`).
- **WERROR compile:** `luofu-switch.o` rebuilt clean under `W=1 -Werror`.
- **checkpatch:** 0 errors, 0 checks, only the expected commit-description warning
  (`logs/11-checkpatch-045.log`).
- No devicetree/binding/Kconfig change; no vendor binary or per-unit value added.
