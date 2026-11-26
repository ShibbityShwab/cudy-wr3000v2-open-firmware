# wifi-ba-tx-impl - evidence tables (T1-T8)

All addresses are `.text` offsets of the pinned LOCAL vendor host module
`hi5622v100_wifi.ko` 2.5.24 (3,581,748 B, ELF ARM, not stripped) and firmware
CPU addresses (CA = file offset + 0x40000, Thumb-2) of
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...311b`). Both are read in place
and never copied into the repo. Markers: **[V]** re-verified from the named
artifact (this lane's objdump run or the committed decode), **[I]** inference,
**[U]** parked.

The host tests inline-replay every row of T1-T3: `src/ba_tx_selftest.c`
(211 standalone / 225 with the driver H2D decode gate) and
`tools/ba_tx_frame_test.py` (99 checks, independent re-derivation).

## T1. The three H2D BA wires (byte KATs, 0x5A5A-tagged HCC header) [V]

HCC header (12 B): `[0]` group nibble, `[1]` high nibble dst (= `[9]`),
low nibble alloc 1, `[2]`/`[3]` per-message field A/B, `[4..5]` u16 total,
`[6..7]` u16 id, `[8]` field C (0), `[10..11]` tag 0x5A5A.
For the single chip / single AP VAP bench the vendor's hmac-dev triple bytes
(`[1].high/[2]/[3]`, sourced from dev+0xfaa/+0xfa9/+0xfa8) are 0 - chip id 0
on both bands (BA REPORT sec. 4) - so the implementation sends `00 01 00 00`
+ total + id + `00 00` + `5a 5a`.

**g0 id 0x0A - BA relay, 44 B (12 + 32).** Host test KAT
(`uid=0x1234, tid=2, status=0, baw=64, timeout=0, amsdu=1, dialog=0x42,
policy=1`):

```
00 01 00 00 2c 00 0a 00 00 00 5a 5a   header: group 0, alloc 1/dst 0, total 44, id 0x0A, tag
03 01 34 12 00 00 00 02 00 00 00 00   payload: type 3, subtype 1, uid, TID @+7
40 00 00 00 00 00 01 00 42 01 00 00   BAW @+0x0C, timeout @+0x10, A-MSDU @+0x12, dialog @+0x14, policy @+0x15
00 00 00 00                           zeroed tail (the fw reads fields we do not set)
```

* subtype 1 = ADDBA-response relay (configures the fw TX BA context);
  subtype 2 = DELBA relay: `03 02 uid_lo uid_hi 00 00 00 tid init 00 ...`
  with the `+0x14` clear flag explicit 0 [V, TABLES T3 of wifi-ba-agg].
* Group byte: **0** - vendor builder `hmac_mgmt_rx_addba_rsp` @ `.text`
  0x45a84: `ldrb r2,[r3]` / `mov r5,#0` / `bfi r2,r5,#0,#4` /
  `strb r2,[r3]`; id: `mov r2,#10` @ 0x45b48 → `[6]`; payload:
  `movw r0,#0x103` stored at hdr+12, user index `ldrh [user+0xa84]` at
  hdr+14 [V this lane].

**g0 id 0x0B - A-MPDU start/end, 20 B (12 + 8).** KATs
(`tid=2, seq=0x0534, uid=0x1234, flag=0`):

```
start: 00 01 00 00 14 00 0b 00 00 00 5a 5a 00 02 34 05 34 12 00 00
end:   00 01 00 00 14 00 0b 00 00 00 5a 5a 01 02 00 00 34 12 00 00
```

* payload `[0]` 0 start / 1 end, `[1]` TID, `[2..3]` seq (start), `[4..5]`
  user key, `[6]` flow flag (start) [V, TABLES T4].
* Group byte: **0** - vendor builders `hmac_mgmt_tx_ampdu_start` @ 0x4626c /
  `_end` @ 0x46434 preserve `hcc_msg_alloc`'s group byte
  (`ldrb r2,[r3]` / `strb r2,[r3]`, no bfi) and set id `mov r2,#11`
  @ 0x46300 / `mov r3,#11` @ 0x464a0; start payload `[12]=0`,
  `[13]=tid`, `[14..15]=seq`, `[16..17]=user+0xa84`; end `[12]=1` [V this
  lane].

**g0 id 0x1C - TX BA notify, 32 B (12 + 20).** KAT (`uid=0x1234, tid=2,
value=0`):

```
00 01 00 00 20 00 1c 00 00 00 5a 5a 34 12 02 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00
```

* payload `[0..1]` u16 user key, `[2]` TID, `[3]` value (0 = pending->active,
  2 = teardown) [V, TABLES T5].
* **THE GROUP BYTE - a documented divergence.** The vendor builder
  `hmac_tx_ba_notify` @ 0x16b90 forces `hdr[0] = 2`:
  `ldrb r3,[r4]` / `mov r2,#2` / `bfi r3,r2,#0,#4` / `strb r3,[r4]`
  @ 0x16be0-0x16bf0, then `mov r3,#28` / `strb r3,[r4,#6]` @ 0x16bf4 →
  group **2**, id 0x1C; payload at +12 = `[r6+0x0a]` u16 (user key), `[2]`
  TID, `[3]` value [V this lane]. The firmware however advertises an id-0x1C
  handler only in the **group-0** receive table (CA 0x5DFA0, TABLES T2), and
  the committed h2d catalogue flags exactly this ("handler in the g0 table,
  not g2"). **Implementation decision:** the notify is posted as
  **group 0 id 0x1C** - the only (group,id) pair the driver's post gate can
  admit (T3), and the notify is informational by the decoded contract
  (REPORT sec. 4.3: not required to establish the session). The vendor
  group-2 byte is byte-documented but would be refused by the prove-gate; we
  do not weaken the gate for an informational message. [I on the firmware
  acceptance; the wire layout is byte-exact modulo the documented group nibble.]

## T2. Firmware dispatch rows and the driver's H2D post gate [V]

Firmware group-0 H2D receive table (CA 0x10CCFC, 12-byte rows):
`0x0A -> CA 0x5FF64` (BA relay config/clear), `0x0B -> CA 0x5E2F4`
(A-MPDU start/end), `0x1C -> CA 0x5DFA0` (TX BA notify).
The driver's H2D catalogue (`h2d_catalogue.inc`, generated from
`../wifi-h2d-catalog`) advertises exactly these three (group 0, non-NULL
handler), which is what `hi5622_frame_post()` requires before any device
write:

    h2d_frame_decode(wire, len, f, H2D_VF_TRANSPORT | H2D_VF_FIRMWARE | H2D_VF_HANDLER)
    h2d_fw_handler_lookup(f->group, f->id)  != NULL && handler_ca != 0

The independent Python test checks the catalogue rows
(g0 0x0A/0x0B/0x1C present) and the unchanged budget
(`allow_frames=16` default; the fire recipes set a budget).

## T3. The two wire paths used (only these; no new device write) [V]

| path | function | gate |
| --- | --- | --- |
| H2D protocol frames | `hi5622_frame_post(ctx, wire, len, what, NULL)` | transport/firmware/handler decode + `allow_frames` budget + gap + armed/ACTIVE/latched boot; `what` = `ba_relay` / `ba_ampdu` / `ba_notify` |
| 802.11 action frames | `himg_build_hcc_mgmt_tx()` -> `hi5622_frame_post(..., "ba_mgmt_tx")` | the proven HCC g0 id 6 carrier (48-B device control + inline frame); frame sanity `himg_mgmt_frame_ok` |
| (read-only) state | `<D>/cfg80211_status` -> `hi5622_ba_glue_status_line()` | no writes |

## T4. The BA action frames (over the mgmt-TX carrier) [V]

24-byte 802.11 management header (FC `D0 00`, da/sa/bssid, seqctl 0),
category 3 at `[24]`:

| frame | len | body |
| --- | ---: | --- |
| ADDBA request | 33 | `[25]=0`, `[26]=dialog`, `[27..28]=param`, `[29..30]=timeout`, `[31..32]=start seq` |
| ADDBA response | 33 | `[25]=1`, `[26]=dialog`, `[27..28]=status`, `[29..30]=param`, `[31..32]=timeout (vendor sends 0)` |
| DELBA | 30 | `[25]=2`, `[26]=0`, `[27]=(tid<<4)\|(initiator<<3)`, `[28..29]=reason` |

BA parameter set = `(baw<<6) | (policy<<1) | amsdu | (tid<<2)`, BAW 1..256
(clamped), default 64, timeout 0. KATs in `src/ba_tx_selftest.c`
(test_frames / test_wires) and `tools/ba_tx_frame_test.py`.

## T5. The TX-direction negotiation sequence (AP as originator) [V decode / I wiring]

    AP data MPDU to an associated station (or fw D2H 2 / 0x1D action 0)
      -> host: tx session state 1 (pending), dialog token, send ADDBA request (33 B)
      -> peer: ADDBA response (33 B)   [received via mgmt RX group1 id13
                                        and/or the fw D2H 0x1D action 1 relay]
      -> host: validate (state, dialog, TID, status), send
               g0 0x0A subtype 1 relay (44 B)  -> firmware per-TID TX BA ctx
               g0 0x1C notify value 0 (32 B)   -> informational
               state 2 (active)
      -> TX path: g0 0x0B start (20 B) on the first MPDU of a burst,
                  g0 0x0B end (20 B) after HI5622_BA_BURST_MAX MPDUs or
                  when the burst goes quiet (glue hold 20 ms)
      -> teardown (peer DELBA / fw D2H 3 / 0x0F / 0x1D action 2):
               g0 0x0A subtype 2 relay (44 B) + g0 0x1C notify value 2
               (32 B) + session cleared

The first MPDU starts the negotiation **in-kernel** (`hi5622_ba_tx_data()`):
idle -> ADDBA request; pending -> no aggregation (`-EAGAIN`); active -> the
0x0B window boundaries. This is what replaces the vendor userspace WAL
commands (`addba_req` 0xBA, `delba_req` 0x324, `ampdu_end`), which stock
hostapd does not have.

## T6. The fail-closed matrix (host-tested) [V]

| ba_enable | builders | entry points | H2D wires | state change |
| --- | --- | --- | --- | --- |
| **0** (default) | return 0 | `-EOPNOTSUPP` | none | none |
| **1** | build | act | on transitions only | yes |
| **>1** | return 0 | `-EINVAL` | none | none |

Additional refusals (all before any post): invalid TID/BAW/address pointers;
ADDBA response with no pending request or a stale dialog; a frame that is not
category-3 action; an unknown fw user index (no peer is guessed); the action
carrier without a committed AP (`-ENOTCONN`); any `hi5622_frame_post()`
refusal (budget/gap/boot/phase) propagates and the session rolls back or
stays pending. The notify is informational: its failure is counted but does
not fail the session.

## T7. Integration points (all six wired, host-test-checked) [V]

| file | hook | when |
| --- | --- | --- |
| `main.c` | `hi5622_ba_glue_init/remove` | probe / remove (work cancellation) |
| `hcc.c` | `hi5622_ba_glue_d2h` | every D2H frame (cheap group-3 + id filter, then deferred) |
| `hi5622_mgmt.c` | `hi5622_ba_glue_mgmt_rx` | every decoded group1 id13 mgmt frame (category-3 action only) |
| `hi5622_data_tx.c` | `hi5622_ba_glue_tx_data` | after a successful AP data ring post (xmit context: spinlock + schedule_work) |
| `hi5622_scan.c` | `hi5622_ba_glue_ap_up/ap_down` + status line | start_ap commit / stop_ap / cfg80211_status |
| `Kbuild` | `hi5622_ba.o hi5622_ba_glue.o` | link |

All posts are deferred to a workqueue: the D2H hook runs in the HCC service
kthread and the TX hook in `ndo_start_xmit` context, neither may run the
sleeping post path inline.

## T8. Pins (base lineage, patch, artefact) [V]

| pin | value |
| --- | --- |
| base tree | `/tmp/r5-build` (the F2/R5 merged-module lineage; reproduce `../wifi-band1-start-send-5/tools/build-r5-order.sh`) |
| base `hi5622_scan.c` / `hi5622_scan_logic.c` / `.h` | `86d6c57e...` / `15c50d34...` / `6af774ef...` |
| base `hi5622_vap_start_g0.h` / `Kbuild` | `81533d9c...` / `1bb51085...` |
| base module (the R5 fire) | `/tmp/r5-artifact/hi5622_wifi.ko`, 383,240 B, sha256 `24f7221b...`, 65 params |
| lane patch | `patches/0001-ba-tx-impl.patch`, 2,258 lines, sha256 `0964fbf7...` |
| stripped module | **406,168 B**, sha256 **`234b3d4b961037e1a5465cc4cc38ab4603b2c71c451d06ac5ba7212b0104d841`** |
| unstripped | 1,515,740 B, sha256 `658a5887a17cbf69e12824bedff40222f9f6909eeda707f9bd13f715afeb8ddf` |
| `.text` | sha256 `702591233a117f0ccf7db2c6ce75ea0ee81c1e4307c10769bda4b7e2d20ec8fc` |
| params | **66** = 65 (R5) + `ba_enable` (read-only 0444, default 0) |
| vermagic / depends / license | `6.18.60 SMP mod_unload ARMv7 p2v8` / `cfg80211` / GPL |

The whole-file sha is reproducible at the default WORK/ART paths
(`/tmp/ba-tx-build`, `/tmp/batx-artifact`); a different path changes the
out-of-tree build metadata (the documented build-id caveat) - then the
`.text`/size/params pins still hold.
