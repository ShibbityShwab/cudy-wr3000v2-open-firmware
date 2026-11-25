# wifi-ax-advanced - evidence tables (T1-T12)

All firmware addresses are **CPU addresses (CA)** of the pinned local image;
CA = file offset + 0x40000.  All module addresses are **link-time .text
section offsets** (VMA 0, so the symbol value is the offset) of the pinned
local vendor module.  Reproduce any row with:

    OBJ=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump
    FW=/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
    KO=/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
    $OBJ -D -b binary -m arm -M force-thumb --adjust-vma=0x40000 --start-address=<CA> --stop-address=<CA+len> $FW
    $OBJ -dr --start-address=<off> --stop-address=<off+len> $KO
    $OBJ -t $KO | grep <symbol>

Grades: **[V]** = instruction/table/value read from the pinned artifacts and
re-asserted by `tools/fw_ax_advanced_re.py --selftest` (221/221 PASS);
**[I]** = inference from surrounding code or the 802.11ax standard, provenance
given; **[U]** = undetermined; **[L]** = the Lead's live-AP bench evidence
(2026-11-27 04:00, /tmp ledger - not re-derivable offline).

Artifacts: firmware 928,920 B sha256 `7fc87e20..311b` (pinned in the tool);
module 3,581,748 B, unstripped (the tool asserts 30+ .text instruction words
and 27 .symtab addresses, not a whole-file hash - local fixture only).

## T1. The two AX control planes [V]

| plane | carrier | body | firmware entry |
| --- | --- | --- | --- |
| static config events | g0 id1, 12-B HCC header + 8-B token + {u16 key; u16 len; body; 4x0xFF} | fixed per key | table A CA 0x10C23C (217 rows) then table B CA 0x103BF8 (88 rows) then worker |
| ALG config text | same g0 id1 carrier, key **0x0128** (table B row 28) | `{u8 n; u8 off[n]; tokens}` text | worker CA 0x8313C then a runtime handler |

The dispatcher CA 0x43D58 (raw bytes asserted): field B = `hdr[3]` selects
the per-module context (`ctx = CA 0xFE578(0, hdr[3])`, 19 boot-created
contexts at CA 0x119758 + k*0xB40), key = `u16 hdr+20`, len = **one byte**
`hdr+22`, body = `hdr+24`, no length/flag/checksum gate; table entries are
8 B `{u16 key; u16 pad; u32 worker|thumb}`; worker called
`worker(r0=ctx, r1=len, r2=body)`.  (Lane ../wifi-fw-dfs-worker decoded the
dispatcher; this lane re-asserts it and adds the AX keys.)

## T2. The ten AX config-event keys [V]

Row/table from the pinned tables; `worker` is the table value (Thumb bit
set), the code address is `worker & ~1`.

| key | table row | worker value | worker CA | host builder (.text) | meaning |
| --- | ---: | ---: | ---: | --- | --- |
| 0x006F | A 106 | 0x081C45 | 0x081C44 | `hmac_config_set_he_gi` 0x753B0 | HE guard interval |
| 0x00E9 | A 184 | 0x044485 | 0x044484 | `hmac_config_ext_twt_ie_switch` 0x61330 | ext-TWT IE enable |
| 0x035D | A 89 | 0x041E5B | 0x041E5A | `hmac_config_twt_enable` 0x15E8E8 | TWT enable/required |
| 0x005B | A 65 | 0x0813BD | 0x0813BC | `hmac_config_set_wmm_mu_edca_params` 0x76250 | MU EDCA field 1 |
| 0x005C | A 66 | 0x081371 | 0x081370 | idem | MU EDCA field 2 |
| 0x005D | A 67 | 0x081325 | 0x081324 | idem | MU EDCA field 3 |
| 0x005E | A 68 | 0x0812D9 | 0x0812D8 | idem | MU EDCA field 4 |
| 0x005F | A 69 | 0x08128D | 0x08128C | idem | MU EDCA field 5 (u8) |
| 0x04C8 | A 15 | 0x0817CD | 0x0817CC | `hmac_config_set_bfer_support` 0x75FCC | beamformer (AP) |
| 0x0335 | A 14 | 0x081891 | 0x081890 | `hmac_config_set_bfee_support` 0x75F0C | beamformee |
| 0x032E | A 12 | 0x0818DD | 0x0818DC | `hmac_config_set_trig_su_bf_fb` 0x75E40 | trigger-based SU BF feedback |
| 0x0353 | A 165 | 0x043BD5 | 0x043BD4 | `hmac_config_set_cyc_sche_switch` 0x161258 | cyclic schedule |
| 0x04B6 | A 190 | 0x049ACB | 0x049ACA | `hmac_config_multiap_sr_enable` 0xC6D3C | multi-AP spatial reuse |
| 0x04B4 | A 189 | 0x04A865 | 0x04A864 | `hmac_config_multiap_tool` 0xC3F20 | multi-AP tool text |
| 0x0128 | B 28 | 0x08313D | 0x08313C | `hmac_config_alg` 0x58E18 | ALG text plane |

## T3. Worker body -> firmware state [V]

State roots: `ctx` = the per-module context CA 0x119758 + k*0xB40 (field B);
`VAP = *(u32)(ctx+0x190)` = the per-VAP object the AP-start ladder binds.
The vendor host builders write a **host mirror** object; the firmware offsets
below are the *firmware* VAP/ctx fields the workers write (asserted
instruction by instruction).

| key | body | write |
| --- | --- | --- |
| 0x006F | `u8 body[0]` | `VAP+0x831 = body[0]`; body[0]==1 -> `VAP+0x1A7.bit7=1`, `VAP+0x1A8.bit5=1`, else bit5 cleared. Host validates body[0] in {1,2,4} (3 refused) |
| 0x00E9 | `u32 body[0]` | `VAP+0x3DE = (body[0] != 0)` |
| 0x035D | `u8 body[0]` | `ctx+0xDA.bit6 = body[0].bit0` (TWT enable), `ctx+0xDA.bit7 = body[0].bit1` (TWT required) |
| 0x032E | `u8 body[0]` | `VAP+0x1A6.bit7 = body[0]` |
| 0x0335 | `u8 body[0]` | `VAP+0x364 = VAP+0x366 = body[0]`, `VAP+0x1A4.bit1 = body[0]` |
| 0x04C8 | `u8 body[0]` 0..3 | tbb 4-way: 0 -> `VAP+0x365=0, VAP+0x367=0`, `VAP+0x1A4 bits0/2=0`; 1 -> both 1, bits 0/2 = 1; 2 -> `VAP+0x367=1`, bit2=1; 3 -> `VAP+0x365=1, VAP+0x367=1`, bits 0/2=1; >3 -> log, no write |
| 0x0353 | `u8 body[0]` 0/1 | band object `O` (from the VAP/module lookup); 1 -> `CA 0xC6034(O+0x1A08)`, 0 -> `CA 0xC52B4(O+0x1A08)` (scheduler re-arm); other -> log |
| 0x04B6 | `u8 body[0]` | `ctx+0xEE = body[0] & 1` (per-module spatial-reuse enable) |
| 0x04B4 | text | one parsed text argument -> 4-entry {name ptr; handler ptr} table CA 0x104520; handler called as (ctx, r1, out) |
| 0x005B..0x005F | `{u16 sel; u16 pad; u32 ac; u32 val}` (12 B) | `VAP[+0x1D4 + 4*(sel-0x5B) + 24*ac] = val`; ac > 3 -> error log; sel 0x5F is the u8 field, others u32; then `ctx+0xD3 = (ctx+0xD3+1) & 0xF` |
| 0x0128 | ALG text | see T7 |

## T4. MU EDCA selectors - host and firmware side [V]/[I]

Host `hmac_config_set_wmm_mu_edca_params(mac, len, body)`:
`u16(body[0]) - 0x5B` selects one of five parameter fields (jump table at
0x76290), `body[4]` is the AC index 0..3 (24-byte stride at the host VAP
`[mac+0x18C]`), `body[8]` is the value; the same 12-byte body is posted
to key `0x5B+selector`.  The firmware workers write the mirror fields at
`VAP+0x1D4..+0x1E4` (T3).

The semantic mapping of the five fields is **[I]**: the standard MU EDCA
Parameter Set is per-AC `{AIFSN, ECWmin, ECWmax, MU EDCA Timer}`, but the
vendor keeps *five* fields per AC (0x1D4/0x1D8/0x1DC/0x1E0 u32 + 0x1E4 u8);
the standard element builder (`mac_set_wmm_mu_edca_params_ie` 0x28028)
reads all five and packs them into the WMM/MU-EDCA IE bytes, so the first
four are the per-AC parameters and the u8 field is a flag/ACM-style bit.
A host implementation that posts one selector is a *decode-faithful* test,
not yet a claim that the on-air MU EDCA element changes.

## T5. TWT - the one end-to-end decoded host path [V]

1. user space: `wal_hipriv_twt_enable` (0x131634) parses one integer and
   accepts only **1 or 3** (0x1316BC: `cmp r0,#3; cmpne r0,#1; bhi`
   error) - 1 = enable, 3 = enable + required (the two bits the worker
   decodes).
2. it posts a WAL cfg event containing `{u8 value; u16 0x035D; u16 4}`
   with the value byte as the body (0x1316DC `movw r3,#0x35D`, 0x1316E0
   `mov r2,#4`).
3. `hmac_config_twt_enable` (0x15E8E8) mirrors the bits into the host
   object (+0xD2 bits 6/7) and posts the same key/body on the g0 carrier.
4. firmware worker CA 0x41E5A writes `ctx+0xDA` bits 6/7 (T3).
5. the per-user TWT agreement itself is a firmware-to-host event handled by
   `hdpp_d2h_user_twt_syn` (0x2B6C): `{u16 uid; u8 flags}`, flags bit0
   -> `user+0x1C8.bit0` (per-STA TWT state sync).  That path needs an
   associated STA, so it is **blocked by the RX gate**.

Summary: TWT **enabling** is postable host-only (29-B g0 frame, read-back
`ctx+0xDA`), TWT **agreements** are not testable without RX.

## T6. MU-MIMO - the vendor machinery and why it is not a config event [V]/[I]

The MU-MIMO/sounding machinery is **not** in table A/B.  The vendor module
carries the whole sounding parameter plane as **ALG parameters** (T8):
`hmac_sounding_config_param` (0xF7E70) reads a userspace parameter *file*
(`filp_open`/`kernel_read`), parses it and calls
`hmac_sounding_config_user_copy` (0xF7C54) which stores the sounding blob
into per-user objects (u16 length at user+0x94); the per-user capability
blocks then travel in the g0 id8 user-add message (../wifi-ax-he-caps T5/T6)
- the HE hdl 58 B block includes the 9-byte HE Operation cache and 10
residual bytes that are the likely per-user TWT/OM/sounding state [I].
The AP-side BF enables **are** config events (0x04C8/0x0335/0x032E, T3):
with the RX gate open the sequence would be "AP start -> BFER/BFEE enable
-> sounding alg params -> NDPA/NDP", and only the first two are
host-testable today.

## T7. The ALG config text plane [V] (frame + dispatch) / [U] (handler body)

Frame: g0 id1 **key 0x0128** (table B row 28, worker CA 0x8313C).

Body (decoded instruction by instruction):

    +0x00  u8   n = number of tokens (>=1; len >= 8)
    +0x01  u8   off[1] ... off[n]  token offsets, relative to body+8
    +0x08.. NUL-separated tokens; token[k] at body+8+off[k]

Worker CA 0x8313C (raw bytes asserted): length gate `cmp r3,#8; bcc`
(0x83148); space-to-NUL canonicalisation (0x8319E); token pointer build
(0x831AC/0x831B0/0x831B4); then for module id 0..2 it compares **token[0]**
with the module name table CA 0x104FC0 and calls
`(*(u32)(CBH + 4*(0xD2+id)))(ctx, n-1, &ptr[1])`, CBH = CA 0x1747C4.

| id | name (CA 0x104FD8) | handler slot (CPU) | value |
| ---: | --- | --- | --- |
| 0 | `sch` | CA 0x174B0C = CBH+0x348 | **[U]** runtime-installed; read-only probe T11 |
| 1 | `cmn` | CA 0x174B10 | **[U]** |
| 2 | `err_name` | CA 0x174B14 | **[U]** |

Host sender `hmac_config_alg` (0x58E18) re-canonicalises the text (len gate
> 8, 0x20 to NUL) and has exactly one host-side special case: token0 `sch`
+ token1 `usr_bw` (relocations .LC187/.LC188 = "sch"/"usr_bw") = per-STA
bandwidth - it resolves the MAC (token 2) with
`mac_vap_get_user_by_addr` and sets a host WAL flag before posting the
frame.  Everything else is forwarded raw.

**The handler body is the remaining decode gap**: its address is a runtime
callback (the same boot hook-block installer CA 0x5B424 that
../wifi-fw-callback-install decoded; it installs about 140 slots and the
three alg slots are not among the immediate-offset stores that lane
enumerated - hence the on-device read in T11).

## T8. The vendor AX parameter vocabulary (what flows on the ALG plane) [V]

Representative .text symbols of the parameter plane (all asserted through the
module .symtab by the tool):

| symbol | .text | role |
| --- | ---: | --- |
| `wal_config_alg_cfg_param_host_entry` | 0x0FC284 | wal entry point for alg-cfg parameter get/set |
| `alg_ofdma_config_param_output_entry` | 0x084468 | OFDMA output block (RU allocation, user_ru, user_tid, mu_seq_type, ofdma_bw, ltf_gi, puncture, certification) |
| `alg_trigger_cfg_param_output_tb_ppdu_ofdma_ba_sw` | 0x077FD0 | trigger/TB-PPDU OFDMA BA switch parameter |
| `alg_sounding_config_param_output_entry` | 0x082BA8 | sounding output block (group property, user nc/codebook/feedback, period, rssi limit, sch mode) |
| `hmac_sounding_config_param` | 0x0F7E70 | sounding param file parser -> per-user object |
| `hmac_sounding_config_user_copy` | 0x0F7C54 | stores the sounding blob (u16 len at user+0x94) |
| `alg_sr_config_param_output_entry` | 0x0799EC | spatial-reuse parameter output block |
| `hmac_alg_sr_config_param` | 0x0963CC | SR param setter; id **0xCE5** -> hmac_sr_cfg_param_bss_color |
| `hmac_sr_cfg_param_bss_color` | 0x0961D4 | writes the color field of the matching station objects |
| `mac_set_sr_ie` | 0x0256D0 | host SR element builder (assoc frames, not the firmware beacon) |
| `wal_hipriv_multiap_sr_enable` | 0x114A38 | user path for the multi-AP SR enable (key 0x04B6) |

The module .rodata.str1.4 carries the full parameter vocabulary
(`alg_*ofdma*`, `alg_*sounding*`, `alg_*sr_*`; 203/164/59 string hits
for ofdma/sounding/twt respectively) - the vendor host parses those names
locally and sends the ALG text frame; the firmware `sch` handler consumes
the token grammar [I].

## T9. BSS color - host decode vs the live on-air IE [V]/[L]

Host builders (module .text, raw bytes asserted):

* `mac_set_he_operation_params` 0x25588: `out[0..2] = LE(4 | ((u16(MIB+0x1BA) >> 6) << 4))`;
  the low nibble 4 is the Default PE Duration; with the vendor TXOP-RTS
  threshold 1023 the result is exactly **0x003FF4** - the value the Lead's
  scanner decoded from the live 2.4 GHz HE AP [L].
* `mac_set_bss_color_info` 0x25604: `out[0] = u8(obj+0xCC) & 0x7F`
  (6-bit BSS color + partial-color bit) [V].
* `hmac_alg_sr_config_param` 0x963CC: ALG param id **0xCE5** ->
  `hmac_sr_cfg_param_bss_color` (per-station color field) [V].
* `hmac_sr_bss_color_change_event` 0x95D6C and
  `hmac_scan_config_bss_color` 0x99C10: the D2H color-change and scan-side
  handlers [V].

**Finding:** the vendor host has **no g0-id1 key** that carries BSS color
(the module's 263 `hmac_config_send_event` call sites were enumerated; the
color writers are the ALG SR param path and the host IE builders), and the
certified negative of lane ../wifi-ax-he-caps says the firmware beacon never
reads host IEs.  With our clean-room driver posting only AP-config mode 10,
the live beacon still carries Color **25** [L]; therefore the color is
firmware-side state (constant or derived - the differential is FIRE-RECIPE
R2).  The host ALG SR param 0x0CE5 path can only be a *control* once the
firmware beacon builder is shown to consume it; today it is not.

## T10. What can be evaluated on the current 2.4 GHz HE AP [L]/[U]

| item | today | test |
| --- | --- | --- |
| HE Capabilities IE | on air (rich firmware caps, not the sparse vendor wiphy caps) | scan once (R0) |
| HE Operation params | on air 0x003FF4, Default PE 4, TXOP RTS 1023 | scan once (R0) |
| BSS Color | 25 | R2 differential: change hostapd `he_bss_color`/SSID/channel, restart, scan; a fixed color = a firmware constant |
| MU EDCA element | **[U]** whether the firmware mode-10 builder emits one | R1: post MU EDCA selectors, then scan/diff |
| TWT element / ext-TWT IE | **[U]** | R1: post 0x00E9 + 0x035D, then scan/diff |
| OFDMA triggers / MU-MIMO sounding / TWT agreements | blocked by the RX gate (association) | not in this lane |

## T11. Read-only probe pack (no writes, one boot) [V addresses]

Firmware RAM phys = `0x40000000 + CA + 0x6B8000`; read with the committed
read-only helper (O_RDONLY on the PCIe resource0).  Static addresses:

| size | CPU | what | expected |
| --- | ---: | --- | --- |
| u32 | 0x171458 | context slot table[0] = context[0] pointer | 0x00119758 |
| u16 | 0x171504 | context base offset (add to every pointer) | 0x0000 [I] |
| u32 | 0x1198E8 | ctx0+0x190 = VAP pointer | nonzero; chase |
| u8 | 0x119832 | ctx0+0xDA TWT bits 6/7 | 0 before an enable |
| u8 | 0x119846 | ctx0+0xEE multi-AP SR enable | 0 default |
| u32 | 0x174B0C | ALG handler sch | nonzero code address [U] |
| u32 | 0x174B10 | ALG handler cmn | nonzero [U] |
| u32 | 0x174B14 | ALG handler err_name | nonzero [U] |
| u32 | 0x174B34 | hook CBH+0x370 (MAC start) | 0 in this image |
| u32 | 0x174B38 | hook CBH+0x374 (beacon start) | 0x00057639 |
| u32 | 0x15E408 | band0+0x1A00 published per-link | 0x00174BF0 |
| u8 | 0x15D90C | band0+0xF04 RF mode byte | 3 after a 0x01F7 |
| u32 | 0x15FF70 | band1+0x1A00 published per-link | 0x00175A18 |
| u8 | 0x15F474 | band1+0xF04 RF mode byte | 3 after F2 |

Second step (pointer chase; `VAP = u32(0x1198E8) + u16(0x171504)`):
`VAP+0x1A4` u8 (BF flags), `+0x1A6` bit7, `+0x1A7` bit7, `+0x1A8`
bit5, `+0x1D4..+0x1E4` MU EDCA (5 fields x 4 AC, 24-B stride),
`+0x364..+0x367`, `+0x3DE`, `+0x831` (T3).  This pack requires zero
writes.

## T12. Our own host-built frames (protocol bytes, not vendor bytes) [V]

g0-id1 envelope (total = 28 + body_len; the fired 0x01F7 frame is reproduced
byte-for-byte by the tool):

    +0x00 12 B  HCC header: 00 | 01 | 00 | fieldB | u16 total | u16 1 | 00 00 | 5A 5A
    +0x0C  8 B  token: 00000000 01000000 (the fired envelope; semantics [U])
    +0x14 u16   key
    +0x16 u16   body length
    +0x18 body
    +end  4 B   0xFF 0xFF 0xFF 0xFF

AX frames this lane's model emits (`--frames`; exact bytes are asserted
against the C model in ../src/): TWT enable (1) and enable+required (3)
29 B; ext TWT IE 32 B; HE GI 1/4 29 B; MU EDCA 0x5B ac0 40 B; BFEE/BFER/
trig/cyc/SR one-byte frames 29 B; ALG text `sch` 8+body B.  Field B must
be the AP-start module context (0 in the fired 2.4 GHz ladder;
../wifi-fw-dfs-worker decoded field B = the per-module context selector).
