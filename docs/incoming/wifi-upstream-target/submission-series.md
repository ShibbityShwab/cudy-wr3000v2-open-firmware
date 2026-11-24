# Proposed submission series (v2) - luofu 6.18 kernel patches + DTS-0001

Status: **planning artefact, 2026-11-24. Nothing has been submitted.** The frozen
review form in ../../upstream/series-6.18/ is the current (v1) 26-patch + DTS-0001
artefact; this page is the ordered v2 that incorporates the patches added to the
target after 2026-11-19 and the fold decisions from PATCH-CLASSIFICATION.md.

Deltas from v1 (execute them by regenerating from the target patches; do not hand-edit
the generated series):

| v1 entry | v2 content | source L0 patches |
| --- | --- | --- |
| 0009 net: luofu-gmac | GEMAC base + max-frame + static MAC config; bench gates dropped | 009 + 038 + 041 (functional hunks) + 051 |
| 0012 thermal | v1 base + v1/hwver-3 read path, gated enable/trim, binding window update | 012 + 066 + 067 |
| 0013 PCIe | host controller + board endpoint reset lines as DT data (reset-gpios), same pulse semantics, instead of hardcoded GPIO0 | 013 + 013a (rewritten) |
| 0019 CRG clock tree | clock tree + 6.18 clk-core read-only mux fix | 019 + 027 |
| 0020 PIE datapath | ring engine + RXCFG word (RX enable) | 020 + 048 |
| 0022 LSW qid-map | write path + final gate-token names | 022 + 044 |
| 0024 PIE RX decode | vendor-exact completion decode is the implementation | 024 + 053 |
| 0027 (new RFC) | net: hisilicon: luofu-switch: program the EQM queue-manager tables | 032 |
| 0028 (new RFC) | net: hisilicon: luofu-switch: initialise the PFE/IFC forwarding tables | 036 + 043 + 046 |
| 0029 (new RFC) | net: hisilicon: luofu-switch: initialise the NAPT flow engine | 059 |
| 0030 (new RFC) | net: hisilicon: luofu-pie: deliver native exception-class RX frames | 060 |

Everything else in v1 (0001-0008, 0010, 0011, 0014-0018, 0021, 0023, 0025, 0026) keeps
its diff; only the per-driver "hardware-verified / unverified" paragraph needs a
refresh. The authoritative v1 messages are ../../upstream/series-6.18/commit-messages/.

## Ordered series (30 kernel patches + 1 DTS patch)

Groups are ordered so the platform boots between every prefix; a reviewer can stop at
the end of any group. Bench-only diagnostics are not present.

| # | subject | notes |
| --- | --- | --- |
| 0001 | ARM: hisilicon: add luofu machine support | ARCH_LUOFU descriptor, PL310 AUX; UP-first, SMP optional |
| 0002 | ARM: add luofu TEXT_OFFSET 0x00608000 | Kconfig/Makefile/textofs wiring |
| 0003 | ARM: head: materialise a non-immediate TEXT_OFFSET | the four early-boot sites |
| 0004 | clk: hisilicon: add luofu CRG clock/reset provider | gates + 2-cell reset; no register writes in probe |
| 0005 | watchdog: add luofu watchdog | child of the CRG syscon |
| 0006 | mtd: spi-nand: add luofu FMC SPI-MEM controller | mainline SPI-NAND stack behind it |
| 0007 | net: mdio: add luofu MDIO controller | |
| 0008 | net: phy: add Genius PHY driver | PHY-ID matched, no DT compatible |
| 0009 | net: ethernet: add luofu GEMAC | + max-frame/W16 static config (fold) |
| 0010 | power: reset: add luofu restart driver | SYSCTRL/CRG, watchdog fallback |
| 0011 | pinctrl: add luofu pinctrl | tables from the vendor module |
| 0012 | thermal: add luofu tvsensor | + v1/hwver-3 path and binding update (fold) |
| 0013 | PCI: dwc: add luofu PCIe host | + DT reset-gpios endpoint pulse (fold) |
| 0014 | clk: hisilicon: luofu-crg: fix reset polarity | active-low release |
| 0015 | net: ethernet: add luofu PIE packet engine (RFC) | refuses to arm without the attending gate |
| 0016 | net: ethernet: add luofu internal switch probe (RFC) | read-only D1a/D1b |
| 0017 | soc: hisilicon: add luofu boot-selector ABI | sysfs ABI doc; loader pair protocol |
| 0018 | ARM: hisilicon: add luofu SMP release | pen/resume/IPI; **send only with DTS-0001** |
| 0019 | clk: hisilicon: luofu-crg: add clock tree | + read-only mux fix (fold) |
| 0020 | net: ethernet: luofu-pie: add ring engine (RFC) | gated data path |
| 0021 | net: ethernet: luofu-switch: add baseline probe (RFC) | read-only W1 |
| 0022 | net: ethernet: luofu-switch: add gated qid-map write path (RFC) | + final option names (fold) |
| 0023 | net: ethernet: luofu-gmac: add netdev readiness gate (RFC) | phylink; registers only when the PIE+LSW path is ready |
| 0024 | net: ethernet: luofu-pie: add RX completion decode (RFC) | + vendor-exact decode (fold) |
| 0025 | net: ethernet: luofu-switch: add egress/probe-configuration stage (RFC) | gated W4 |
| 0026 | net: ethernet: luofu-gmac: add bdinfo MAC nvmem source | read-only factory partition |
| 0027 | net: ethernet: luofu-switch: add EQM queue-manager init (RFC) | new |
| 0028 | net: ethernet: luofu-switch: add PFE/IFC init and default tables (RFC) | new |
| 0029 | net: ethernet: luofu-switch: add NAPT flow-engine init (RFC) | new |
| 0030 | net: ethernet: luofu-pie: add native exception-class RX delivery (RFC) | new |
| DTS-0001 | ARM: dts: hisilicon: add luofu-r116 board | loader hand-off nodes + board nodes; regenerate from the single source after GP1 |

Per-patch hardware/evidence status ([V] vs [U]) stays in the commit messages. The
cover letter must say plainly: the platform boots on the bench, the network data path
is gated/attended, Wi-Fi is not part of this series.

## Commit-message drafts for the new RFC entries

~~~
net: ethernet: hisilicon: luofu-switch: program the EQM queue-manager tables

The internal switch's egress path needs the measurement-derived EQM tables
programmed before the queue manager accepts frames: table 19 (cell count),
32 (per queue-port deque), 35 (per qid buffer), 17 (scheduler profile),
16 (per qid tag row) and 34 (per qid deque map) for the nine wired queue
groups (ports 4..12, qids 1..72).  The values are computed from the
pre-write snapshot plus the vendor-formula relations documented in
LSW-SPEC.md, never copied from a vendor table dump.

Write verification is tiered: fail-closed on the fields whose read view is
bench-proven, diagnostic-only for the tables the engine does not read back
faithfully, and an acknowledge-only path for the write-only table.  Any
mismatch restores the bench-proven entries and powers the block down.

The write sequence remains behind the attending gate; a normal boot keeps
the block read-only.  Unverified: restore of the write-only tables, the
table-38 scheduler class, the ports 40..43 virtual-port class.
~~~

~~~
net: ethernet: hisilicon: luofu-switch: initialise the PFE/IFC forwarding tables

Program the forwarding engine and the ingress classifier default tables the
vendor stack installs at switch bring-up: the PFE block power/reset state,
the IFC default-classification rows and the extended EPS/DP stage.  The
table contents are derived from the measured live baseline (2026-11-18
inventory) and the vendor init order, and every write is read back; the
block is restored and powered down on mismatch.

Gated: this runs only on the attended bench boot; a normal boot leaves the
switch untouched.  Unverified: exact classification row semantics and the
EPS extended class behaviour without the vendor bridge.
~~~

~~~
net: ethernet: hisilicon: luofu-switch: initialise the NAPT flow engine

Initialise the internal switch's NAPT/flow engine so forwarded TCP/UDP flows
can be accelerated: the measured init write set is applied behind the
existing attending gate, followed by a read-back of every written word and
a restore on mismatch.

This is what makes the bench image's native CPU packet delivery work; it is
still a written-once, power-on-only sequence with no per-flow programming in
the driver.  Unverified: per-flow table management and the exact accelerator
tie-in used by the vendor engine.
~~~

~~~
net: ethernet: hisilicon: luofu-pie: deliver native exception-class RX frames

The PIE RX completion carries an exception class for frames the MAC could
not classify; the vendor stack delivers those frames to the port's netdev
instead of dropping them.  Implement that delivery and retire the
bench-only accepterr option, keeping the existing completion decode and the
buffer re-arm path unchanged.

Unverified: the precise error-cause mapping per exception class.  The normal
(non-exception) path is unchanged and the engine stays behind the attending
arm option until the normal-boot bring-up is verified.
~~~

## Acceptance criteria

A patch may leave the review form only when ALL of these hold.

**Host (mechanical)**
- [ ] 30-patch series applies to pristine linux-6.18.60 with patch(1) and git am in
      order, identical resulting trees;
- [ ] checkpatch.pl 0 errors (warnings triaged; 0 for new files);
- [ ] full kernel compile RC=0 with the OpenWrt GCC 14.4.0 toolchain and W=1 clean for
      the touched files;
- [ ] dt_binding_check RC=0 for every binding; dtc W=1 clean for the DTS;
- [ ] the target image chain builds host-side from exactly this series;
- [ ] gen-series.py --check clean and validation/sha256sums.txt regenerated.

**Design (review)**
- [ ] no luofu_* module parameters / bench gates in the submission patches: gates
      become DT data (e.g. 0013 reset-gpios) or are removed;
- [ ] no debugfs/printk diagnostics left behind (the DROP list stays out);
- [ ] every compatible has a reviewed binding and a documented evidence grade;
- [ ] vendor-derived tables are described with their derivation method, not dumped;
- [ ] reset/clock/power claims are limited to the hardware-verified subset, with the
      unverified items listed in the commit message;
- [ ] MAINTAINERS entry and tree-placement rationale updated.

**Hardware ("do not send a claim you have not measured")**
- [ ] every active-DTS change boot-tested on the unit (project rule);
- [ ] platform group (0001-0014, 0017-0019): boots the unit, 2 CPUs, serial, NAND/UBI;
- [ ] PCIe/thermal/pinctrl/GPIO: the specific feature exercised, unverified paths listed;
- [ ] PIE/LSW RFC group: no traffic/support claim in the cover letter; the attending
      gate is documented as board bring-up tooling, not a user feature;
- [ ] SMP patch only with DTS-0001 carrying the two loader hand-off nodes.

**External (not kernel)**
- [ ] the Wi-Fi answer (GPL source request / closed blob / rewrite scope) is stated as
      out of scope for this series;
- [ ] the target PR is separate and only after the install path ran end to end on a
      recovered unit;
- [ ] publication checklist (history purge, licences) is separate from the review form.

## Regeneration procedure (for the lane that executes it)

~~~
# in opensource/upstream/series-6.18, against pristine linux-6.18.60:
python3 tools/gen-series.py --check          # drift check
python3 tools/gen-series.py                  # regenerate series/ from target patches
tools/run-validation.sh                      # apply/git-am, checkpatch, bindings, DTS
python3 tools/check-release-line.sh          # L1 manifest/validator (55 -> 56 after 013a)
~~~

The fold decisions are content changes, not just regeneration: make them in the target
patches first (with the hardware evidence), then regenerate.
