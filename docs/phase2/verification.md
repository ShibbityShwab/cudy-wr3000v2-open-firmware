# Phase-2 independent verification

Independent re-run of every claim in `power-decode.md` and `alg-dispatch.md` against the raw
artifacts. Read-only, local files only, no device access. Every row below was produced by
executing the command shown in this session (venv `pyenv/Scripts/python.exe`, pyelftools +
capstone). PASS rows are reproduced, not restated.

Artifacts:
- `build/tmp/cfg_wifi.ini` (1015 lines)
- `build/tmp/hi5622v100_wifi.ko` (3,564,728 bytes, ET_REL ARM)
- `build/cal-snapshots/20260930-195439/{alg-values.txt,MANIFEST.sha256}`

---

## 1. `power-decode.md` (decode lane)

All commands run with cwd `C:/Users/ShibbityShwab/router-openwrt/build/tmp` unless stated.

| # | Claim in `power-decode.md` | Exact command run | Observed output | Verdict |
|---|---|---|---|---|
| P1 | Whole-file `^pwr_limit_` count = **332** | `grep -c '^pwr_limit_' cfg_wifi.ini` | `332` | **PASS** |
| P2 | Per-section split with section tracking: `[HOST_WIFI_NORMAL]` 166 + `[HOST_WIFI_PWR_LIMIT_LU]` 166 | `awk '/^\[/{sec=$0} /^pwr_limit_/{cnt[sec]++} END{for(s in cnt) print s, cnt[s]}' cfg_wifi.ini` | `[HOST_WIFI_PWR_LIMIT_LU] 166`<br>`[HOST_WIFI_NORMAL] 166` | **PASS** |
| P3 | LU section holds 186 key lines = 166 `pwr_limit_*` + 20 non-`pwr_limit` | `sed -n '/^\[HOST_WIFI_PWR_LIMIT_LU\]/,/^\[CUSTOM_REGDOMAIN_CFG\]/p' cfg_wifi.ini \| grep -c '^[a-zA-Z]'` ; and `... \| grep '^[a-zA-Z]' \| grep -vc '^pwr_limit_'` | `186` and `20` | **PASS** |
| P4 | Whole-file six-value histogram: `12`x13, `18`x14, `1E`x42, `24`x22, `32`x33, `3C`x208 (sum 332) | `grep '^pwr_limit_' cfg_wifi.ini \| sed 's/.*=//' \| sort \| uniq -c` | `13 0x00121212` / `14 0x00181818` / `42 0x001E1E1E` / `22 0x00242424` / `33 0x00323232` / `208 0x003C3C3C` (sum 332) | **PASS** |
| P5 | LU-section histogram: same bytes but `0x003C3C3C` x42 (sum 166) | `sed -n '/^\[HOST_WIFI_PWR_LIMIT_LU\]/,/^\[CUSTOM_REGDOMAIN_CFG\]/p' cfg_wifi.ini \| grep '^pwr_limit_' \| sed 's/.*=//' \| sort \| uniq -c` | `13 0x00121212` / `14 0x00181818` / `42 0x001E1E1E` / `22 0x00242424` / `33 0x00323232` / `42 0x003C3C3C` (sum 166) | **PASS** |
| P6 | Whole-file band split: 2g 112 + 5g 220 | `grep '^pwr_limit_' cfg_wifi.ini \| sed 's/^pwr_limit_\([0-9a-z]*\)_.*/\1/' \| sort \| uniq -c` | `112 2g` / `220 5g` | **PASS** |
| P7 | LU-section band split: 2g 56 + 5g 110 | `sed -n '/^\[HOST_WIFI_PWR_LIMIT_LU\]/,/^\[CUSTOM_REGDOMAIN_CFG\]/p' cfg_wifi.ini \| grep '^pwr_limit_' \| sed 's/^pwr_limit_\([0-9a-z]*\)_.*/\1/' \| sort \| uniq -c` | `56 2g` / `110 5g` | **PASS** |
| P8 | Every `pwr_limit_*` value is `0x00xxxxxx` with three identical payload bytes (166/166 in LU; 332/332 whole file) | `grep '^pwr_limit_' cfg_wifi.ini \| grep -c '=0x00\(..\)\1\1$'` | `332` (out of `332` total) | **PASS** |
| P9 | Regdomain `ALPHA2` keys = 3 | `sed -n '/^\[CUSTOM_REGDOMAIN_CFG\]/,$p' cfg_wifi.ini \| grep -c '^custom_regdomain_.*_ALPHA2='` | `3` | **PASS** |
| P10 | Regdomain `BAND` keys = 13 (BZ 5 + DE 4 + LU 4) | `sed -n '/^\[CUSTOM_REGDOMAIN_CFG\]/,$p' cfg_wifi.ini \| grep -c '^custom_regdomain_.*_BAND[0-9]='` | `13` | **PASS** |
| P11 | 0.5 dBm/LSB arithmetic: max payload byte `0x3C`=60 -> 30 dBm, matching the regdomain 30 dBm ceiling; exactly 6 distinct payload bytes | `grep '^pwr_limit_' cfg_wifi.ini \| sed 's/.*=0x00//' \| cut -c1-2 \| sort -u` and `pyenv/Scripts/python.exe -c "v=0x3C;print(v,v/2)"` | distinct bytes `12 18 1E 24 32 3C`; max `3C`; `60 30.0` | **PASS** |
| P12 | 2.4 GHz calibration tuple count = **26** | cwd `build/cal-snapshots/20260930-195439`: `grep -A1 '=== get_2g_power_param ===' alg-values.txt \| tail -1 \| sed 's/.*\[SUCC\]//' \| wc -w` | `26` | **PASS** |
| P13 | 5 GHz calibration tuple count = **18** | cwd `build/cal-snapshots/20260930-195439`: `grep -A1 '=== get_5g_power_param ===' alg-values.txt \| tail -1 \| sed 's/.*\[SUCC\]//' \| wc -w` | `18` | **PASS** |

Notes: the `sed`/`awk`/`grep` numbers reproduce the document exactly, including the reconciliation
`166 + 166 = 332` and the whole-file vs section-scoped distinction. The document's claim that the
2.4 GHz list "as supplied contains 26" (not 27) is reproduced by the whitespace split (P12).

---

## 2. `alg-dispatch.md` (disassembly lane)

cwd `C:/Users/ShibbityShwab/router-openwrt`.

| # | Claim in `alg-dispatch.md` | Exact command run | Observed output | Verdict |
|---|---|---|---|---|
| D1 | The 10 named symbols exist with the stated section-relative addresses and sizes | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));S=e.get_section_by_name('.symtab');W=['wal_algcmd_char_extra_adapt','wal_wlan_cfg_module_process_entry','wal_wlan_cfg_alg_process_entry','wal_wlan_cfg_cali_process_entry','alg_cfg_search_process_info_by_cfg_name','g_ast_alg_cfg_process_info_table','g_alg_cfg_lut','hmac_sync_dmac_alg_cfg_rsp_entry','hmac_sync_dmac_cali_cfg_rsp_entry','hmac_chan_tx_cali_sync'];[print(n,hex(s['st_value']),s['st_size']) for n in W for s in S.iter_symbols() if s.name==n]"` | `wal_algcmd_char_extra_adapt 0x10c564 700` / `wal_wlan_cfg_module_process_entry 0x127af8 972` / `wal_wlan_cfg_alg_process_entry 0x14b46c 640` / `wal_wlan_cfg_cali_process_entry 0x14b6ec 456` / `alg_cfg_search_process_info_by_cfg_name 0x14aefc 184` / `g_ast_alg_cfg_process_info_table 0x35c8 4968` / `g_alg_cfg_lut 0x4934 73216` / `hmac_sync_dmac_alg_cfg_rsp_entry 0x87d70 352` / `hmac_sync_dmac_cali_cfg_rsp_entry 0x881a4 148` / `hmac_chan_tx_cali_sync 0x46dac 280` | **PASS** |
| D2 | `.symtab` = 21,495 symbols, 3,847 `STT_FUNC`, 0 zero-sized functions | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));S=e.get_section_by_name('.symtab');a=list(S.iter_symbols());f=[s for s in a if s['st_info']['type']=='STT_FUNC'];print('total',len(a),'func',len(f),'zero-size',sum(1 for s in f if s['st_size']==0))"` | `total 21495 func 3847 zero-size 0` | **PASS** |
| D3 | 12-byte module table at `.data+0x1d58`: `[0]` cmd `0x0101`, `[1]` cmd `0x0102` (stride 0xc) | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;import struct;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));d=open('build/tmp/hi5622v100_wifi.ko','rb').read();o=e.get_section_by_name('.data')['sh_offset'];r=d[o+0x1d58:o+0x1d58+24];[print('entry',i,'@.data+0x%x'%(0x1d58+i*12),'cmd=0x%04x'%struct.unpack_from('<H',r,i*12)[0],'flags=0x%08x'%struct.unpack_from('<I',r,i*12+4)[0]) for i in range(2)]"` | `entry 0 @.data+0x1d58 cmd=0x0101 flags=0x01010101`<br>`entry 1 @.data+0x1d64 cmd=0x0102 flags=0x01010101` (raw bytes: `010100000101010100000000` / `020100000101010100000000`) | **PASS** |
| D4 | `.rel.data` relocation at offset **0x1d60** names `wal_wlan_cfg_alg_process_entry` | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));S=e.get_section_by_name('.symtab');[print(hex(r['r_offset']),S.get_symbol(r['r_info_sym']).name) for r in e.get_section_by_name('.rel.data').iter_relocations() if r['r_offset'] in (0x1d60,0x1d6c)]"` | `0x1d60 wal_wlan_cfg_alg_process_entry` | **PASS** |
| D5 | `.rel.data` relocation at offset **0x1d6c** names `wal_wlan_cfg_cali_process_entry` | (same command as D4) | `0x1d6c wal_wlan_cfg_cali_process_entry` | **PASS** |
| D6 | `g_ast_alg_cfg_process_info_table` claimed 414 x 12 bytes = 4968 | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));[print(s.name,s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols() if s.name in ('g_ast_alg_cfg_process_info_table','g_alg_cfg_lut')]"` | `g_ast_alg_cfg_process_info_table 4968` (`4968 == 414*12`) | **PASS** |
| D7 | `g_alg_cfg_lut` claimed 416 x 176 bytes = 73216 | (same command as D6) | `g_alg_cfg_lut 73216` (`73216 == 416*176`) | **PASS** |
| D8 | LUT entries carry the stated cfg_id / dir / handler (idx 216-235: 0x0dae/0x0db0/0x0db2/0x0db3, both dirs -> `alg_cfg_args_param_analyse_equipment_param`) | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;import struct;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));d=open('build/tmp/hi5622v100_wifi.ko','rb').read();o=e.get_section_by_name('.data')['sh_offset'];S=e.get_section_by_name('.symtab');R={x['r_offset']:S.get_symbol(x['r_info_sym']).name for x in e.get_section_by_name('.rel.data').iter_relocations()};[print('idx',i,'cfg_id=0x%04x'%struct.unpack_from('<H',d,o+0x4934+i*0xb0)[0],'dir',d[o+0x4934+i*0xb0+2],R.get(0x4934+i*0xb0+8)) for i in (216,217,218,219,226,227,234,235)]"` | `idx 216 cfg_id=0x0dae dir 0 alg_cfg_args_param_analyse_equipment_param` / `idx 217 cfg_id=0x0dae dir 1 ...` / `idx 218/219 0x0db0` / `idx 226/227 0x0db2` / `idx 234/235 0x0db3`, all +8 handler `alg_cfg_args_param_analyse_equipment_param` | **PASS** |
| D9 | Name table row `[350] @.data+0x4630 name='set_2g_power_param' cfg_id=0x0dae dir=0` | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;import struct;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));d=open('build/tmp/hi5622v100_wifi.ko','rb').read();o=e.get_section_by_name('.data')['sh_offset'];p=struct.unpack_from('<I',d,o+0x4630)[0];s=e.get_section_by_name('.rodata.str1.4');print(d[s['sh_offset']+p:d.index(b'\x00',s['sh_offset']+p)].decode(),'cfg_id=0x%04x'%struct.unpack_from('<H',d,o+0x4634)[0],'dir',d[o+0x4636])"` | `set_2g_power_param cfg_id=0x0dae dir 0` (raw name pointer `0xab768`) | **PASS** |
| D10 | Address convention: every section has `sh_addr = 0` (section-relative addresses) | `./pyenv/Scripts/python.exe -c "from elftools.elf.elffile import ELFFile;e=ELFFile(open('build/tmp/hi5622v100_wifi.ko','rb'));ns=[(s.name,s['sh_addr']) for s in e.iter_sections() if s['sh_addr']!=0];print('sections with sh_addr!=0:',len(ns),ns)"` | `sections with sh_addr!=0: 0 []`; section offsets/sizes `.text off=0x38 size=0x1630cc`, `.rodata off=0x167b00 size=0x25264`, `.rodata.str1.4 off=0x191e40 size=0xb040b`, `.data off=0x243ab0 size=0x16768`, `.symtab off=0x26331c` | **PASS** |
| D11 | The "not proven" section (S5) is honest | Read of `alg-dispatch.md` §5 against D1–D10 scope | **Agree.** §5 correctly limits itself to (1) the wext path that reaches `wal_algcmd_char_extra_adapt`, (2) full 416-entry LUT coverage (only 170 have a +8 pointer), (3) the firmware wire format, (4) the table tail bytes, (5) the absence of a standalone `alg` C-string. Each is genuinely outside what D1–D10 establish: D1 confirms the symbol landscape but not the wext indexing path (and 0x0101 < `SIOCIWFIRSTPRIV`, so the standard `private[]` indexing really does not apply); D8 confirms only the sampled LUT rows, not all 416; nothing here disassembles the firmware message layout. No overclaim. | **PASS** |

Note on D6/D7: the two symbol sizes are exactly `414 * 12 = 4968` and `416 * 176 = 73216`, so the
claimed entry counts and strides are confirmed by the symbol sizes alone.

---

## 3. Calibration snapshot integrity

cwd `C:/Users/ShibbityShwab/router-openwrt/build/cal-snapshots/20260930-195439`.

| # | Claim | Exact command run | Observed output | Verdict |
|---|---|---|---|---|
| S1 | Snapshot matches its SHA-256 manifest | `sha256sum -c MANIFEST.sha256` | `FIRMWARE.bin: OK` / `alg-values.txt: OK` / `cfg_device_hisi.ini: OK` / `cfg_hi5622v100_hisi.ini: OK` / `factory-cal.txt: OK` / `identity.txt: OK` / `module-params.txt: OK` / `wireless-config.txt: OK` (exit 0) | **PASS** |

---

## OVERALL

- Claims checked: **25** (P1-P13, D1-D11, S1)
- PASS: **25**
- FAIL: **0**
- Overall: **PASS** - all 25 claims independently reproduced against the raw artifacts; no
  discrepancy found. `power-decode.md` and `alg-dispatch.md` are corroborated by this session's
  own command output, and the snapshot is intact (8/8 files OK).
