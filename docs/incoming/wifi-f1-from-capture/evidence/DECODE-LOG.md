# DECODE-LOG - inputs, commands, hashes (host-only, nothing vendor committed)

## Inputs (local, never committed)

| artifact | identity |
| --- | --- |
| capture | `/tmp/h2d-capture.log`, 2603 lines, 1716 M-records, sha256 `970b80064896da8eaf11272119f1c70fd6a04bfb3003e34c5f899804f75b1679`; sampler header: `# omo_h2dsampler v2 ts0=5884 window=0x583f2000 module=hi5622v100_plat duration=240 period_ms=20 access=mmap` |
| firmware | `recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`; CA = file + 0x40000 |
| plat module | `recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko`, 371,708 B, sha256 `27c697918bc00f37afd490a812edff49cd2df9f8ac1d791ce2ca79c87adbba2b` (objdump -dr local decode) |
| wifi module | `recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko`, 3,581,748 B, sha256 `22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2` |
| disassembler | `/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump -dr` (modules) + capstone thumb (firmware) |

## Commands

```
python3 tools/decode_f1_capture.py --selftest                 # 5/5 PASS
python3 tools/decode_f1_capture.py /tmp/h2d-capture.log       # -> evidence/FAMILY-TABLE.txt
python3 tools/decode_f1_capture.py /tmp/h2d-capture.log --json
```

Tool decode of the capture (re-runnable, stdlib only):

```
records 1716 ; valid H2D 604 ; torn 1112 (unreadable 250)
alid H2D families: g0 id29 329 ; g0 id1 key 0x00d8 104 ; key 0x000b 57 ; key 0x0127 38 ;
key 0x07e8 17 ; key 0x07d9 7 ; key 0x0069 7 ; key 0x0005 5 ; key 0x0052 5 ; key 0x0073 4 ;
key 0x0065 4 ; key 0x01cf/0x0124/0x015d/0x07e7 3 each ; key 0x0000 2 ; g0 id2 2 ; (1 each:
0x01f7, 0x025f, 0x011e, 0x00f7, 0x0078, 0x02bd, 0x006f, 0x03e4, 0x003c, 0x01ee, 0x0004 absent)
alias: g12 id1 -> g0 id1 key 0x0127 (18 records)
```

The vendor-builder and firmware-handler address claims were decoded from the two local modules and
the firmware with the toolchain/capstone as recorded in REPORT.md sec. 2-3 (function addresses,
immediates and hook slots only; no vendor bytes quoted). The capture's protocol bytes are quoted
in the reports as decoded text only.
