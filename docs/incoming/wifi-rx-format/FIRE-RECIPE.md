# FIRE-RECIPE - wifi-rx-format (task-163): the read-only device-write witness run

**The Lead fires the bench. This lane is host-only and never executes a
router stage itself.** This recipe is designed to be *cheap and read-only*: it
can ride on the next association fire and answer "did the MAC ever DMA a
received frame into host memory?" without moving a single ring pointer.

## 0. What is being decided

The open bug has two independent halves:

* **(A) no capture / no mgmt RX event** - management frames are expected on
  the D2H CRX path (g1 id 13), not on the data rings; the ring format is not
  the suspect (section 7.1 of [REPORT.md](REPORT.md)).
* **(B) the firmware exception** - with `cmp_wptr = 0` the drain performs zero
  writes, so identify the exception class separately (page/mask/excp
  sampling); the only dangerous RX write is a free-ring producer publish,
  which the patched driver now guards (REPORT section 7.2).

`dp_rx_probe` answers (A) empirically: it reads the two RX pools and reports a
witness (host-word cookie intact + nonzero 36-byte header) for any buffer the
MAC DMA'd into.  It never writes.

## 1. Preconditions (all checked read-only by the script)

* The **patched module** is loaded: `/sys/module/hi5622_wifi` present and
  `dp_rx_probe` exists under the PCI device dir (0444).
* The association ladder in THIS load reached:
  `AP -> dp_setup -> dp_txq_sync -> dp_rx_arm` (same order as
  `../wifi-assoc-e2e/RUNBOOK.md`).  The self-check inside `dp_setup` has
  already verified the rings/config against the vendor stock mirror.
* Recovery door armed (physical power-cycle) per
  `BENCH-FACTS.md`/`tools/recovery-card.sh`; one load per boot; staging 0.

## 2. Rungs (all read-only; no ring pointer is moved by any of them)

### F1 - probe before any client traffic (baseline)

On the router:

    RXF_DRY_RUN=0 ./tools/route-rx-probe.sh --probe
    RXF_DRY_RUN=0 ./tools/route-rx-probe.sh --snapshot

Record the `dp_rx_probe` line + page/mask/dmesg snapshot.  Expected on a
healthy armed idle AP: `pool: ... witness=0 untouched=768 stale=0` and
`cmp_ring: nonzero_entries=0`.

### F2 - probe while the client tries to authenticate

Run F1's `--probe` again while the laptop (or phone) attempts a connection
(auth/assoc attempts are enough; association is not required for the
management path).  Then run F1's `--snapshot`.

### F3 - probe with an associated data source (only if F2 needs a data datum)

If association works, generate data traffic (EAPOL msg2/msg4 happen during the
4-way; else ARP/DHCP/ICMP) and re-run `--probe`.  This is where the free-ring
rptr / completion wptr may finally move (data path).

### F4 - crash detection (rides on every rung)

Every `--snapshot` records the page line, the exception mask/announce and the
dmesg tail.  If the golden page is lost or `excp_announce=1` appears: **STOP,
do not retry in the same load, power-cycle physically** (the SoC reboot does
not reset the Wi-Fi chip), and keep the snapshot as the (B) finding.

## 3. Verdict decision table (from the `dp_rx_probe` last line)

| verdict | meaning | next action |
| --- | --- | --- |
| `FORMAT-SETUP-MISMATCH` | a config/ring word does not mirror the vendor stock dump (on the patched module this should be impossible - bind would have refused `dp_setup`) | STOP: record `cfg MISMATCH` lines + `dp_status`; do not debug capture |
| `DEVICE-WROTE-FRAMES` | the MAC DID DMA a received frame into host memory (cookie intact, header nonzero) but the completion/report path did not surface it | this is the decisive capture-path finding: record `cmp_ring nonzero_entries` / `cmp_wptr` / the witness header hex; the completion-pointer path (wptr publish, consumer, IRQ/poll) is the bug, not the format |
| `COMPLETION-ENTRIES-NONZERO-BUT-NO-FRAME-HEADER` | completion entries exist but no frame header - the ring saw a completion without a per-buffer write (stale/foreign entry) | record, STOP; the buffer/entry association is broken somewhere upstream |
| `MAC-WROTE-NOTHING` | rings armed and configured; no received frame reached host memory in this load | for mgmt-only traffic this is the expected architecture (D2H CRX path); with an associated data source it points at the firmware/VAP forwarding path, not the entry/header format |

## 4. Safety

* The script never writes: it cat's sysfs files, reads the page snapshot and
  tail's dmesg.  It is safe with `rx_enable=0` and before `dp_rx_arm` (the
  probe requires only `bound`, i.e. a successful `dp_setup`).
* Never reload the module expecting a different result in the same load; a
  page/mask deviation means physical power-cycle.
* The drain (`dp_rx_drain`) is **not** needed for the witness verdict; if the
  association lane's drain loop is already running, the probe remains valid
  (read-only) and should be taken between drain ticks.

---

## 5. Follow-up (2026-11-25): stock-control run + fault-localisation (R2b result)

R2 observe-only showed `frames=0 bad=0 chains=0` (no host buffer written) and
**page golden:0 during the auth attempts** -> the receive path faults the
device/firmware DMAC.  The PPDU pre-write plan was dropped (REPORT section 10);
no lane module is required for the next datum - it is a **stock-control** run.

### SC-1 - vendor image control (the decisive rung)

Boot the stock vendor image (vendor host driver + vendor AP bring-up, no
clean-room module), bring the AP up the vendor way, and let the laptop send
the same 3 auth frames.  Record: page/golden, vector mask, staging, dmesg
`rx_ppdu_desc` lines, live bank CA 0x40050000 (status, +0x04!, +0x08/+0x0c,
+0x10), and whether the vendor AP logs the auth.

| outcome | meaning |
| --- | --- |
| vendor receives/auths, page stays golden | the fault is in our clean-room session configuration (firmware-side RX state/F1 or status-forward F4) - the vendor's pre-RX host sequence is the thing to emulate |
| vendor also loses the page on the same frames | the fault class is upstream (RF/PHY/MAC or firmware bug), not our host setup |
| vendor receives but logs `rx_ppdu_desc error` | bit 10 is confirmed as benign/informational (it is latched and masked in the stock dump already) |

### SC-2 - diagnostics that need no new module (read-only)

* Record live +0x04 before/after the auth trigger (ours booted as 0x8000,
  vendor stock 0), plus the full live window for the crash plan.
* Record whether our status bits 1/8 (asserted since boot as 0x182) correlate
  with any dmesg/forwarding difference vs stock.
* On the stock-control boot, capture the vendor's H2D message sequence if the
  image exposes it; the firmware handlers `smac_msg_proc_enable_trx_req` /
  `smac_msg_proc_rx_ring_reset_req` suggest a pre-RX request that our
  clean-room driver does not build (no sender in the two vendor modules we
  decoded - other vendor host component/message name).

Every rung: one load per boot, attended, recovery door armed; STOP on any
golden-page deviation and power-cycle physically.

---

## 6. Follow-up 2 (2026-11-25): F4 forwarding decode + pre-RX request negative

Full decode and implementation spec: REPORT section 11.

**F4 (implement next, safe half):** the vendor host-MAC ISR forwards status bit 0/1 as an HCC **group 0 / id 0** message (12-byte header, 4-byte payload {u32 dev}, tag/len by the carrier; firmware handler is the payload-free stub) and W1Cs the served bit. Bits 5/7/8/9 go out as **group 0 / id 7** (16-byte payload {u32 dev, u8 3, u32 status-word}; firmware handler reads the payload - not emulated yet). Our 0x182 boot status has bit 1 and bit 8 active: a rx_status_forward=1 one-shot rung would send the g0 id0 message for bit 1, then W1C bits 0/1 to config +0x44, read back and verify; expected observable change: the status word loses bit 1 on read-back and no page deviation.

**Pre-RX request - negative result (do not chase a missing H2D message):** firmware Table A (g0-id1 key table) has only 8 live keys (0x0004, 0x64, 0x65, 0x66, 0x67, 0x69, 0xd8, 0x320), none trx/ring-reset; the smac_msg_proc_enable_trx_req / rx_ring_reset_req names are a debug-string blob with no ID table and are firmware-internal; the vendor rootfs has no userspace sender (only module load + calibration iwpriv). Next static target: the vendor **plat** module's init-time bal_msg_send/hcc_msg_tx messages (PCIe/device boot side). Stock-control run (SC-1) is still the decisive experiment; with F4 implemented it also has the forwarding difference to watch.
