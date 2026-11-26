# SPECTRAL - the Hi5622V100 PSD spectral feature: decoded surface, missing decode, plan

**Lane:** `monitor-mode` (task-241). Host-only static decode. No device contact.
Grades: **[V]** verified here from the device-version module; **[V\*]** committed lane;
**[M]** bench-measured earlier; **[I]** inference; **[U]** unknown.

## 0. Result in one line

Spectral is a **host-driven PSD (power-spectral-density) MAC block**, not a firmware message
feature: three 32-bit registers at bank +0x430/+0x434/+0x438 are configured through 19
`hal_set_psd_*` read-modify-write accessors, read back through 17 `hal_get_psd_*`, and the
plan is reached from the host WAL alg-cfg entry; init/reinit at boot and on a protection-info
event. The **full bitfield map is decoded here** (TABLES T6) - what is missing is the bank's
**CA base** and the **report/data memory** layout, which is one host-only decode away.

## 1. What the vendor feature is (device version)

* The 2.5.24 module contains the whole spectral family (TABLES T5): `hmac_spectral_init/exit`,
  `hmac_get_spectral`, `hmac_alg_spectral_scan_config_param`,
  `hmac_spectral_{dlist_choose,get_count,scan_complete,scan_timeout_fn}`,
  `hmac_spectral_*_SHUANGTA` (per-chip: alloc/free/data-print/count/get/set param),
  `shuangta_set_spectral_{psd_en,data_addr,rd_len,intr_delay_time}`,
  `shuangta_get_spectral_scan_data{in,out}_abnormal`, `shuangta_init_spectral_param_defaults`,
  `shuangta_spectral_print_ddr_data_to_file`, and the 37 `hal_*_psd_*` register accessors [V].
* Init is pure host-side state: `hmac_spectral_init` (0x8a320) loops the two bands, memsets
  two 120-byte state blocks, initialises spinlocks and calls
  `shuangta_init_spectral_param_defaults`; it sends **no H2D message** [V].
* Init callers: `hmac_main_init+0xf4` (boot) and `hmac_protection_info_syn_event+0x51c`
  (band/protection re-init) [V].
* The host command path: WAL alg-cfg param host entry
  (`wal_config_alg_cfg_param_host_entry+0x338`) -> `hmac_alg_spectral_scan_config_param`
  (0x8a530) -> `hmac_get_spectral` (0x896ec) -> `hmac_spectral_set_scan_param_proc_SHUANGTA`
  (0x8ad08) -> the 19 PSD setters [V]. The setters are plain RMW on the mapped bank.
* The data side: PSD results go to a **data address** the host programs
  (`shuangta_set_spectral_data_addr`), with a **read length**
  (`shuangta_set_spectral_rd_len`), an interrupt delay, and over/under-run counters; the
  debug dumper `shuangta_spectral_print_ddr_data_to_file` reads the chip memory to a file
  [V]. The report plumbing uses the LMI/event registers
  `cfg_lmi_ctrl1_init_event_addr` (+0x528), `cfg_lmi_ctrl2_lmi_psd_en` (+0x52c bit 9),
  `cfg_lmi_ctrl3_event_mem_max_num` (+0x530), `cfg_lmi_ctrl6_event_rpt_num_en` (+0x53c)
  (TABLES T6) [V].
* The alg-cfg command surface (cfg ids 1401-1410, names `spectral_*`/`get_spectral_*`) is in
  the 414-entry table of both releases [V\*, phase10]; **2.5.24 deleted the 24 host-side set
  argument parsers** while the table is unchanged, and the live 2.5.24 device still answers
  the seven get-side spectral commands with `[SUCC]` and the 2.4.15-default values [M,
  phase10 \2-3]. So on our firmware the stock *get* path is live and the stock *set* path is
  dead.

## 2. The decoded register map

See TABLES T6 (all 21 fields, bits, setter/getter addresses). Summary:

| reg | off | fields |
| --- | ---: | --- |
| psd_cfg0 | +0x430 | `fft_size`[2:0], `fft_period`[15:4], `count`[31:16] |
| psd_cfg1 | +0x434 | `bottom_noise`[7:0], `rssi_nb`[15:8], `the_power_nb`[22:16], `the_num_nb`[31:23] |
| psd_cfg2 | +0x438 | `rpt_mode`[0], `agc_lock_en`[1], `fftin_len_type`[3:2], `ofdm_det_en`[4], `11b_det_en`[5], `nb_det_en`[6], `psd_en`[8], `ant_index`[9], `p2dbm_offset`[21:12], `deci_coef_man`[25:24], `deci_coef_sel`[26], `auto_cg_disable`[28], `auto_rst_disable`[29], `fft_cken`[30] |

The register struct pointer comes from the mapped-bank object: `shuangta_set_spectral_psd_en`
loads `[spectral_state+0x150]` then `[+0x10]` (the bank pointer) and writes bit 8 of +0x438
[V]. The bank is the same family the RX lanes decoded from the .rodata CA table (10 mapped
banks; T8 S-1).

## 3. What our stack already has

* A portable WAL alg-cfg codec with the 414-entry process table and the spectral rows
  (`wifi-algcfg-slice`, committed): the command encoding/decoding carrier exists as a
  separate module (`hi5622_alg_cfg_codec.ko`, 97 KB, GPL, builds on 6.18.60) [V\*]. It is
  **not** part of the IMAGE #3 merged module - integrating it is a prerequisite for the
  "stock command" route.
* The driver already has the MMIO/BAR0 translation and the physread tooling used by the RX
  lanes; the `hal_*` register accessors are the same kind of access [I].
* The WAL config-event envelope (g0 id1 / WAL group-2) is implemented for the AP path; a
  spectral config event would use it if a firmware-side message existed [I].

## 4. What is missing (the next decode, host-only)

| id | missing | how to decode (host-only) |
| --- | --- | --- |
| S-1 | the **CA base** of the bank holding +0x430/+0x434/+0x438 and the LMI regs +0x528..+0x53c | decode the driver's .rodata mapped-bank table and the HAL object initialisation (the RX lanes already decoded the same table family: 10 banks, band0 word5 = 0x40040000, word8 = 0x40050000) |
| S-2 | **data/report memory**: what `init_event_addr` must point to, where PSD reports land, how the host reads them | static: `shuangta_set_spectral_data_addr/rd_len`, `hmac_spectral_alloc_addr_SHUANGTA`, the LMI regs; bench: read-only |
| S-3 | the **activation sequence/timings** (psd_en / fft_cken / rpt_mode / count / period order) and delays | static order in `hmac_spectral_set_scan_param_proc_SHUANGTA` + `intr_delay_time`; bench: register round-trip |
| S-4 | the **report format** (per-report record: count, FFT size, per-bin power/RSSI) | static `get_count/param_proc` + the DDR data dump; bench: one capture |
| M-1/M-2 | a firmware promiscuous switch / VAP mode 1 (monitor) | separate firmware decode (TABLES T8); not needed for spectral |

**No register write may be attempted until S-1 is decoded** - writing an unverified CA is a
bench risk with no recovery from a host-only lane.

## 5. Implementation options (recommendation)

1. **(recommended) Clean-room register interface.** After S-1..S-3: add a fail-closed
   `spectral=0/1` module param + a read-only status/sysfs surface, decoding the register
   accessors directly. This bypasses the removed 2.5.24 alg-cfg setters entirely and keeps
   the feature independent of the vendored `hi5622_alg_cfg_codec.ko`.
2. **(rejected) Revive the 2.4.15 alg-cfg set parsers.** The 2.4.15 module is not the device
   firmware version; porting 24 deleted parsers is more host code and carries the same
   unknown-firmware risk. The 2.5.24 set path is dead by construction.
3. **(not technical) Ask the firmware.** No GPL source for the hi5622 firmware exists; the
   GPL-2.0 sibling tree has no spectral code at all [V] - family reference stops at the
   monitor/PSD-adjacent architecture.

## 6. PASS ladder (the eventual feature gate)

| rung | gate | PASS criterion |
| --- | --- | --- |
| S0 | static decode S-1 | the three PSD registers + LMI regs have a CA address, each with a static derivation this document can cite |
| S1 | attended read-only probe | reads of the three registers return stable, plausible values (no 0xFFFFFFFF, no abort), page/mask untouched |
| S2 | register round-trip | write `psd_fft_size/count/period` + read back exactly; write `psd_en=1` then 0, read back; all under fail-closed param and one-word writes; page golden after |
| S3 | PSD capture | with a known interferer on the channel, the data/report memory shows spectral energy at the interferer's frequency; a second run without it shows the floor (differential PASS) |
| S4 | deliverable | a documented sysfs/debugfs interface + a capture recipe; no automatic writes at load |

FIRE-RECIPE marks S0-S4 blocked until S-1 lands.

## 7. Honest limits

* All of this is a static decode of the **device-version module**; nothing was run on
  silicon by this lane. The register meanings are names+bits from the vendor accessors;
  their RF semantics are [I] where marked.
* The PSD data format and the data-memory read mechanism are undecoded; no claim that a
  working spectral capture is close - it is, however, host-side work, not a firmware blocker.
* `monitor=1` (local mirror) does **not** produce spectral data; the two features are
  independent.
