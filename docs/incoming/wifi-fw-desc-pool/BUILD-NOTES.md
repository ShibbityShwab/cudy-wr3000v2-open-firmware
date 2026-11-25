# BUILD-NOTES - lane wifi-fw-desc-pool (task-184)

**HOST-ONLY.** Nothing here touched the router, the bench link, UART, or any
device. All evidence was produced offline from the pinned local artifacts.

## Local artifacts (read in place, never committed)

| artifact | path | identity |
| --- | --- | --- |
| firmware | /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin | 928,920 B, sha256 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b |
| bench copy | /home/shibbityshwab/pd4-rebuilt.bin | 928,920 B, same decode space (unused) |
| plat module | /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko | 371,708 B |
| wifi module | /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko | 3,581,748 B |
| vendor ini | .../lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini | 32,315 B |

## Toolchain

* OpenWrt lab ARM binutils:
  /home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/
  toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/
  (arm-openwrt-linux-objdump, arm-openwrt-linux-nm)
* firmware disassembly: `objdump -D -b binary -m arm -M force-thumb
  --adjust-vma=0x40000 FIRMWARE.bin` (CPU = file + 0x40000)
* module disassembly: `objdump -dr hi5622v100_plat.ko` (ELF sections:
  .text Off 0x38, .rodata.str1.4 Off 0x212A4, .data Off 0x31F60, all Addr 0;
  so .text symbol A is at file offset A+0x38)
* Python 3 (stock /usr/bin/python3) only; no extra packages.

## Reproduce

```
cd opensource/docs/incoming/wifi-fw-desc-pool
python3 tools/fw_desc_pool_re.py --selftest     # 97 PASS
python3 tools/fw_desc_pool_re.py --decode
python3 tools/fw_desc_pool_re.py --tables
python3 tools/fw_desc_pool_re.py --policy
python3 tools/fw_desc_pool_re.py --tbtt
python3 tools/fw_desc_pool_re.py --probes
```

Evidence outputs are captured in evidence/ (no vendor bytes: only derived
addresses, instruction windows and small constants).

## Known conventions / corrections applied

* CPU = file + 0x40000; BAR0 = CPU + 0x6B8000; phys = 0x40000000 + BAR0
  (= CPU + 0x46B8000). Validated against committed probe values
  (ctx 0x171458 -> phys 0x40829458, band object 0x15CA08 -> 0x40814A08).
* Correction to lane wifi-fw-rf-programme TABLES T7 row 20: CPU 0x117AD4 has
  BAR0 0x7CFAD4 (phys 0x407CFAD4), not 0x872AD4.
* `plat.ko` is ARM mode for the functions this lane cited; the firmware image
  is Thumb-2 (some helper ranges such as CA 0x1037BC were not needed beyond
  their call identity, recorded as [I]).
* Scratch decode files (full firmware disassembly, module disassembly) live
  under build/wifi-fw-desc-pool/ (gitignored); they are not evidence and are
  not committed.

## Publication boundary

No vendor binary, no firmware byte, no extracted table is committed: the tool
reads the pinned local artifacts at run time and prints only derived addresses
and decode results. No credential, MAC, serial or calibration value appears in
this lane.
