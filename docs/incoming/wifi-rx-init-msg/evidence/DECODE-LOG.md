# DECODE-LOG - commands, anchors and hashes behind REPORT.md

All work was host-only and read-only. The vendor module and firmware are local artifacts and are
**not** committed; only derived text is.

## Artifacts

| artifact | path | identity |
| --- | --- | --- |
| vendor Wi-Fi module | `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko` | 3,581,748 B, sha256 `22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2`, ELF32 ARM EABI5 relocatable, not stripped |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` |

Tooling: `build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump` (project toolchain),
`.provision-venv/bin/python` with `pyelftools 0.33` and `capstone 5.0.7`.

## Exact commands

```
# 0. local evidence copy (gitignored build/)
mkdir -p build/rx-init-msg && cp .../hi5622v100_wifi.ko build/rx-init-msg/wifi.ko

# 1. symbol inventory
nm -n build/rx-init-msg/wifi.ko | wc -l          # 21970
nm build/rx-init-msg/wifi.ko | grep -c ' U '     # 308

# 2. full disassembly WITH relocations (ET_REL: .text sh_addr = 0)
<toolchain>/arm-none-linux-gnueabihf-objdump -dr build/rx-init-msg/wifi.ko > build/rx-init-msg/wifi.dis
wc -l build/rx-init-msg/wifi.dis                 # 437446

# 3. extraction (committed tools)
.provision-venv/bin/python tools/vendor_config_senders.py --ko build/rx-init-msg/wifi.ko
.provision-venv/bin/python tools/vendor_msg_scan.py        --ko build/rx-init-msg/wifi.ko

# 4. firmware key worker (file offset = CA - 0x40000; worker is Thumb)
.provision-venv/bin/python tools/fw_key_worker.py --fw .../FIRMWARE.bin --key 0x0004

# 5. the key-table reference tool from the committed earlier lane
.provision-venv/bin/python ../wifi-ctx-create/tools/ctx_create_re.py --fw .../FIRMWARE.bin --tables
```

## Claim -> evidence map

| claim | how verified |
| --- | --- |
| module is unstripped and carries the full function symbol set | `nm`: 21,970 symbol lines, 7,157 `T/t`, 308 undefined; no DWARF sections in `readelf -S` |
| the init chain sends no H2D message | call-relocation scan: direct callees of `hmac_main_init`(0x3f6b8), `hdpp_main_init`(0x12e0), `hdpp_chip_init`(0x502c), `hdpp_var_init`(0x43f0) enumerated; closure to depth 4 = 124 functions; only HCC calls are registrations + `hcc_set_state` (+ `hmac_txq_table_addr_sync` at depth 4 via `hmac_txq_table_init`, not part of the main_init tree) |
| `hdpp_config_send_event` geometry | instruction-by-instruction disassembly 0x59c0-0x5b00: `hcc_msg_alloc(len+16)`, `msg+0x118` header, `hdr[6]=1`, `strh key,[hdr+20]`, `strh len,[hdr+22]`, `memcpy hdr+24`, `hcc_msg_tx` |
| `hmac_config_send_event` geometry | 0x5717c + shared builder 0x57090: builder writes `strh r8,[hdr,#6]` (=1) and returns data=hdr+12; caller writes key at hdr+20, len at hdr+22, data at hdr+24 |
| 244 distinct config-event keys / 266 sites | `tools/vendor_config_senders.py`: relocation scan for calls to both constructors + backward register scan for `r1`; 5 dynamic-key sites listed in TABLES |
| 77 direct message builders | `tools/vendor_msg_scan.py`: relocation scan for `hcc_msg_tx`/`hcc_msg_tx_to_core`/`hmac_hcc_msg_tx_send` callers; immediate `strb [hdr+6]` scan |
| key 0x0004 has no sender | 266 config sites contain no `r1==4`; header scan over 4,245 functions finds no `hdr[6]=1` + `hdr+20/22 = 4` pair; the 5 dynamic sites are WMM/EDCA/PM wrappers whose callers pass no 4 |
| firmware key table A live keys | committed `wifi-ctx-create/tools/ctx_create_re.py --tables` (re-run): A[0]=0x0004/CA 0x811CD, A[1]=0x0064/0x449E5, A[2]=0x0065/0x46395, A[3]=0x0066/0x4455B, A[4]=0x0067/0x45F75, A[5]=0x0069/0x839B5, A[41]=0x00D8/0x81039, A[164]=0x0320/0x81445 |
| key 0x0004 worker | capstone Thumb at file 0x411cc: `cbz r2`; `ldr.w r3,[r0,#0x190]`; `ldr r2,[r2]`; `strb.w r2,[r3,#0x52]`; `bx lr`; refusal path `movw r0,#0x8b2d` |
| live key -> vendor sender join | TABLE-config-senders.md rows marked LIVE: 0x64 `hmac_config_add_vap_sync_all`@0x15becc, 0x65 `hmac_config_start_vap_event`@0x15afdc, 0x66 `hmac_config_del_vap`@0x15a924, 0x67 `hmac_config_down_vap`@0x15d79c, 0x69 `hmac_set_mode_event`@0x15b080, 0xD8 `hmac_config_mib_amsdu_aggregate_ative`@0x6269c, 0x320 `hmac_txq_table_addr_sync`@0x1af64 (hdpp builder) |
| D2H registration tables at init | relocations in `.rel.text` at 0x3f760/0x3f764 resolve the `.LANCHOR1+.data` table (base .data+0x584, count 28); literals at 0x3f958/0x3f95c are `.data`-relative `R_ARM_ABS32` with addends 0x6d4/0x764 (counts 12/8); `wal_main_init` @0x12386c registers a count-19 table (`mov r2,#19` at 0x123890) |

## Limits

* The module drop differs from the phase37 record (3,564,728 B vs 3,581,748 B); both table
  generators take `--ko`, so a re-run for the other drop is one command.
* A linear scan for a reader of `ctx[+0x190][0x52]` found none within 120 B of the 961
  `[reg,#0x190]` loads; the field's consumer remains [U] (it may be read via an argument-held
  pointer).
* No stock-boot H2D trace exists in the repository; whether a working vendor boot sends key
  0x0004 is [U] and needs the bench.
