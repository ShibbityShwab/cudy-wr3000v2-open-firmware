# FIRE-RECIPE - storing the app/vendor IE on IMAGE #3 (host-only recipe; not fired here)

**Lane appie2 (task-244), HOST-ONLY.** Nothing here was fired, no bench contact was made,
no firmware was written. This is the recipe for the next attended bench fire, revised by
this lane's decode. It builds on - and in one place corrects - the first pass's ladder in
`../appie-wext2/FIRE-RECIPE.md`.

## The result this recipe targets

A client STA scanning the AP sees the vendor 0xDD application IE(s) in the probe
response / beacon, i.e. the firmware per-VAP app-IE slot for the configured type is
populated and the firmware builds the frame from it. The class sweep proved the HCC ring
accepts and consumes our 85-byte g0 id 0x20 frame while the slot stayed empty
(`vap+0x24c+20*type` read back 0xffffffff), so the bench must change the *call
shape*, not the frame bytes.

## The chain to replicate (decoded, see TABLES.md W4/W5)

```
runtime config update on a RUNNING AP netdev
  -> host hmac chain: first 0x7f IE strip (if present)
  -> mac_vap_save_app_ie          (host mirror; must succeed)
  -> hmac_config_h2d_send_app_ie  (the byte-exact g0 id 0x20 frame)
```

The vendor's group-2 / id-0 536-byte envelope is a **host-internal** container (W5). It
is not the firmware storage mechanism, so it is **not** a fire rung here.

## Preconditions (all must hold before the first post)

1. Recovery door armed and attended (`tools/fire-preflight.sh`,
   `tools/recovery-card.sh`), STATUS.md names the delivery plan.
2. The running IMAGE #3 module is the app-ie-image3 lineage (or appie-wext2's patch
   applied on top); build identity recorded (module sha, params count).
3. A **read-only** slot readback channel is available and proven in the same session
   (the existing debug interface that reads `vap+0x24c+20*type` without writes).
4. hostapd is configured with the target 0xDD IE(s) via the existing app_ie module
   parameters so the collector builds the same byte-exact frame the class sweep used.
5. The module parameters are at their defaults before each rung; one rung, one fire, one
   readout.

## The ladder

### H0 - control (no fire)

Baseline on the running stack: slot readback empty, client scan has no 0xDD IE, dmesg
`app_ie:` status line records zero posts. Confirms the starting state and the readback.

### H1 - delayed running-AP post with the netdev context trio (recommended first rung)

The single highest-value delta (D1+D2 in TABLES W9). Apply the app-ie-image3/wext2 patch
with:

* `app_ie_mode=0` (direct g0 id 0x20 frame - the frame the class sweep proved is
  consumed),
* `app_ie_when=1` (deferred ~1 s after `start_ap()` returns, i.e. on the running AP),
* **and** build the frame's context trio from the netdev hmac context (`dev+0x328`)
  instead of the VAP-start module value (which was 0 in every class-sweep fire). If the
  patch cannot source the real context yet, keep the trio sweep (H2) for round two, but
  do not skip the delayed timing.

Acceptance: dmesg shows exactly one deferred post after the AP enable; the slot readback
for the configured type is non-0xffffffff with the expected len/type; a client scan sees
the 0xDD IE; no ring/consumer anomaly; system stays up. If the slot is still empty, go
H2 - do not repeat H1 with the same inputs.

### H2 - hdr[3] / context sweep at running AP time

With the unmodified app-ie-image3 module (no rebuild), sweep the frame context byte
(hdr[3]) 0..7 at the delayed running-AP moment, one value per post, reading the slot after
each. This isolates the VAP/chip context lookup in the firmware g0 id 0x20 handler from
the timing delta. Acceptance = one value populates the slot; record which.

### H3 - envelope rung: REMOVED by this lane

The first pass proposed posting the vendor-exact 536-byte g2/id0 envelope over HCC and
watching for a cfg-event response. This lane's decode shows the envelope is built and
consumed on the **host** (`wal_recv_config_cmd` -> `wal_config_process_pkt`, no tx;
TABLES W5/W6/W7). There is no firmware g2/id0 consumer to store the slot (consistent with
the sibling's firmware receive-table census finding no id 0), so this rung would spend a
fire on a message the firmware is not expected to act on. If someone still wants to
falsify the finding, the cheap read-only check is the local firmware receive-table census
plus a D2H cfg-event census, not a bench fire.

### Optional later rung - private-ioctl emulation

Only if H1/H2 leave the slot empty: expose the full vendor call shape on our side by
adding the WEXT private ioctl `0x8eec` handler that parses the vendor userspace struct
and calls the same hmac chain (parse/body/order already modelled in `src/`). This
tests the hypothesis that the firmware handler keys off the WEXT-originated call path
rather than the frame alone. It is a larger change and should wait for H1/H2 results.

## Rules

* One rung per fire; never stack changes; record the module identity and parameters.
* Slot readback is read-only and must be proven before any post.
* If any rung shows a ring anomaly, error-log burst or the AP stops beaconing, stop and
  recover - do not iterate on the module.
* No vendor binary or firmware byte goes into tracked text; evidence stays under
  `build/` (gitignored) and only the distilled result goes to the ledger.
