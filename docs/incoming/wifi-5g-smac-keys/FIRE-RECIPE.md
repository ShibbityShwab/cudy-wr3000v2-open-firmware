# FIRE-RECIPE - the 20 stock-only keys vs the band-1 SMAC device chain

**Lane:** wifi-5g-smac-keys (task-223), 2026-11-26. **This lane never fires.** The Lead fires with
the resident bench procedure: bench link PASS, recovery card READY, human at the bench, UART capture
read-only. **One change per fire, one rung per fresh boot, no in-place re-post of a consumed frame.**

Inherit all non-negotiables from ../wifi-band1-start-send/FIRE-RECIPE.md,
../wifi-band1-start-send-3/FIRE-RECIPE.md and ../wifi-band1-start-send-5/FIRE-RECIPE.md: recovery
armed, UART read-only (never type to the tty), never write CA 0x400392F0, never read the RC misc
window 0x10160000..0x1016FFFF, never `rmmod` the vendor modules, page/state/mask anomaly ->
power-cycle, no blind flash.

**Module under test:** the deployed R5-order module of ../wifi-band1-start-send-5 (383,240 B sha256
`24f7221b...a70c`, 65 params) **plus one generic g0-id1 "candidate post" hook** that posts one extra
config event `{key, body}` at a named plan position and stamps it like the existing
`frame post '...' ... consumed` lines. No knob changes for R1; R2-R5 each add exactly one post.

**Why a candidate post at all [V decode]:** the stock sends 20 g0-id1 config keys our ladder does
not (TABLES T2/T7 of this lane). None of them is the boot per-band SMAC attach (that is the
boot-only CA 0xD1B68 loop, which writes the chain-A base for both bands). The only keys with a path
into the band-1 SMAC/band-start machinery are 0x0073 (state), 0x0069 (mode), 0x0000 (chain-A MAC
registers) and 0x07E8 (per-channel TX-cali sync) - TABLES T8. R0 must confirm the premise before any
candidate is posted.

---

## R0 - read-only premise probes (same boot as the bench baseline, NO new writes)

Use the committed `bar0read` helper (physical addresses, O_RDONLY + PROT_READ, refuses unaligned
addresses and the RC misc window). Read-only, two-step where noted.

| # | object | phys | width | expectation |
| ---: | --- | ---: | ---: | --- |
| P0a | wrapper +8 (band0 object ptr) | 0x4083881C | 4 | 0x174BF0 [M] |
| P0b | wrapper +0x1C (band1 object ptr) | 0x40838830 | 4 | 0x175A18 [M] |
| P1a/b | band0 obj **+640** / **+672** | 0x4082CE70 / 0x4082CE90 | 4 | 0x114F18 / **0x40042000** |
| P2a/b | band1 obj **+640** / **+672** | 0x4082DC98 / 0x4082DCB8 | 4 | 0x114F18 / **0x40062000** |
| P3 | band0 chain A +0xE4 / +0xE8 | 0x403DA0E4 / 0x403DA0E8 | 4 | 0x00A00096 / 0 [M] |
| P4 | band1 chain A +0xE4 / +0xE8 | 0x403FA0E4 / 0x403FA0E8 | 4 | **0 / 0** [M] |
| P5 | band-1 SMAC arm word (send-5 P1) | 0x404C4220 | 4 | 0x1029 armed / 0 never armed |
| P6 | chain-A per-chain MAC regs (idx0), both bands | 0x403DA0EC/0x403DA0F4 / 0x403FA0EC/0x403FA0F4 | 4 | [U] |

If P0a/P0b differ from the literals, add 0x280 / 0x2A0 to the *runtime* pointer and use those
addresses instead - do not reuse the literals.

**R0 verdict matrix (STOP/GO):**

* **H1 (uniform model):** P1b = 0x40042000 and P2b = 0x40062000 (both boot defaults), P1a = P2a =
  0x114F18. Then the record's "working band0 +672 = RAM block" was a +0x20 probe slip. **The premise
  collapses: do not post the 20 keys.** Re-scope to the device-word difference (P3 vs P4, P5) - the
  remaining gap is the band-1 chain not producing ISRs / not armed, a device-side or host-MMIO
  question, not a g0-id1 key.
* **H2 (post-boot overwrite):** P1b = 0x114F18 (the +640 value) while P2b = 0x40062000, i.e. band0's
  +672 was overwritten after boot. Then the overwriter is not in the 20 keys (no such store in their
  bodies), but the trigger may still be a missing host step. Proceed to R1-R5, one rung per boot.

Record: all eight words, the two runtime object pointers, P5/P6, plus the R5 baseline dmesg.

## R1 - baseline rung (no new post)

Fire the R5 recipe unchanged (same module, same 65 knobs, ch40 AP; dmesg order plan 9 steps:
vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg vap_start_g0 band_start ap_vap_start). Record:
P3/P4/P5/P6 + 'iw dev OBS scan freq 5200' (no beacon expected). This is the reference for R2-R5.

## R2 - candidate 0x0073 (hmac_config_vap_state_syn), body 1 B = state

Fresh boot, R5 ladder unchanged, **one** extra post after the `ap_vap_start` post is consumed:

    post g0 id1 key 0x0073  body = {04}        # VAP FSM state 4 (start branch; handler calls band start CA 0x971D8)

If P4/P5 do not move: **R2b**, fresh boot, body = `{07}` (the state-7 branch -> CA 0x97008); then
**R2c** body = `{01}` only if R2/R2b show a partial move. Mark P4/P5/P6 + beacon.

## R3 - candidate 0x0069 (hmac_set_mode_event), body 4 B = {state, b17, b18}

Fresh boot, R5 ladder unchanged, **one** extra post after `ap_cfg` (mode-set context):

    post g0 id1 key 0x0069  body = {04 00 00}

If no movement: R3b `{05 00 00}`, R3c `{07 00 00}` (one value per fresh boot). The handler stores
payload[0] -> `ctx+15`, [1] -> `ctx+17`, [2] -> `ctx+18/29` and drives the firmware VAP FSM.

## R4 - candidate 0x0000 (hmac_config_set_mac_addr), body 6 B = wlan5 MAC

Fresh boot, R5 ladder unchanged, **one** extra post after `vap_add`/`desc_table` (the stock's
VAP-add context):

    post g0 id1 key 0x0000  body = <6 B of the wlan5 interface MAC>

This is the only stock-only handler that writes the SMAC chain-A registers (per-chain MAC slots at
`[obj+672]+idx*0x80+0xEC/0xF0/0xF4/0xF8`); watch P6 and P3/P4.

## R5 - candidate 0x07E8 (hmac_chan_tx_cali_sync), body 1 B = band

Fresh boot, R5 ladder unchanged, **one** extra post after `ap_chan`:

    post g0 id1 key 0x07E8  body = {01}        # band index 1 (5 GHz)

Stock call-site context: `hmac_chan_start_bss` (17 sends per stock boot). Watch P3/P4/P6 + beacon.

## R6 - only if one single rung moved a probe

Combine that rung's post with the R5 ladder in the stock order and re-probe. Do not stack more than
two candidates per boot.

---

## PASS / FAIL / ABORT

* **Rung PASS** on any of: P4 (band1 chain +0xE4) becomes nonzero; P5 changes from its R1 value;
  P6 band-1 MAC slots change; **or the 5200 MHz observer sees the ch40 beacon** (the real rung).
* **Rung FAIL**: every probe identical to R1. Revert the post, next rung. 40 B/plan order unchanged.
* **ABORT the fire immediately** (power-cycle, no in-place retry): page/state/mask gate not golden,
  'consumed' lines do not appear for a posted frame, a firmware exception frame, any UART register
  dump, or a probe read that requires a write. Never write CA 0x400392F0; never read the RC misc
  window; never `rmmod` the vendor modules.
* **Interpretation honesty:** if R0 = H1, the 20-key line of inquiry is closed (negative result) and
  the next discriminator is the device word P3/P4/P5, not a config key. If R0 = H2 and no rung moves
  a probe, the residual is firmware-internal (the boot loop's post-boot destination for +672), which
  needs a patch lane, not more posts.

## Address conversion (read-only probe helper)

    device IO   phys = CA + 0x3B8000
    firmware RAM phys = 0x40000000 + CA + 0x6B8000
    band object = wrapper[+8] (band0) / wrapper[+0x1C] (band1); wrapper var CA 0x180814
    chain A base = u32[band_object + 0x2A0]; status word = u32[chain A base + 0xE4]
