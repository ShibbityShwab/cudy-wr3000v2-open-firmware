# calread: the chip's live calibration tables, read back word-for-word (phase 12, 2026-10-01)

**Result: an open, read-only module (`lab/calread`) maps the chip's BAR0 memory window, copies the
live 576-byte calibration block at BAR0 `0x1b2f00`, and exposes it through
`/sys/kernel/debug/calread/tables`. Its two power tables match the vendor's own
`iwpriv Hisilicon0 alg get_2g_power_param` (26 words) and `get_5g_power_param` (18 words) word for
word - all 44 words identical, with identical md5 of the normalized word lists. `insmod`/`cat`/
`rmmod` all returned 0, the device did not reboot (uptime `11:29` before and after), and no vendor
module was loaded, unloaded, or otherwise touched.**

This is the first OPEN function we own on the device: the vendor's own answer to "what are my
calibration tables" is reproduced from the raw memory window by a module we build.

## The module

- Source: `lab/calread/calread.c` + `lab/calread/Makefile` in this repo.
- On load it:
  1. finds endpoint `0000:00:00.0` (`59e7:0005`) with `pci_get_domain_bus_and_slot()`, and reads
     the BAR0 base from PCI config space with `pci_read_config_dword()` - a read-only vendor-kernel
     accessor, so no `struct pci_dev` layout risk;
  2. `ioremap()`s 576 bytes at BAR0 `+0x1b2f00` - **no `pci_request_region`, no
     `pci_enable_device`, no claim, no reset**; the vendor driver keeps ownership throughout;
  3. `memcpy_fromio()`s the block into a local buffer once and renders it as text - the 576 raw
     bytes in 16-byte hex rows, plus the 2.4 GHz table (`block+0x24`, 26 words) and the 5 GHz table
     (`block+0x1ea`, 18 words) as 32-bit little-endian hex words;
  4. creates `/sys/kernel/debug/calread/tables` (mode 0444) to serve that text;
  5. `pr_info`s the same two tables to dmesg.
- The 5 GHz table starts at a 2-byte-aligned address (offset mod 4 == 2), so the module assembles
  its words bytewise from the copied block rather than using aligned `ioread32()`s.
- On unload it removes the debugfs entries, frees the buffers, `iounmap()`s the window and drops the
  PCI device reference. Nothing is written to the chip anywhere.

## CI

- Workflow: `.github/workflows/build-load-test-module.yml` - added a `build calread module` step, a
  vermagic check line, and a `calread-ko` artifact.
- Run: **`36837345627`** -
  https://github.com/ShibbityShwab/cudy-wr3000v2-open-firmware/actions/runs/36837345627
  - commit `1d9f5fd`, conclusion `success`.
  - artifact `calread-ko` = `calread.ko`, 9540 bytes, md5 `7147ee18270b065e0b770bbe62d10589`,
    `vermagic=5.10.201 SMP mod_unload ARMv7` (matches the vendor kernel).

## On-device run

The CI artifact was `scp`'d to `/tmp/calread.ko`; the device md5
`7147ee18270b065e0b770bbe62d10589` equals the local artifact. Sequence and result:

    ===BEFORE===   Thu Oct  1 09:23:16 UTC 2026, up 11:29
    ===MD5===      7147ee18270b065e0b770bbe62d10589  /tmp/calread.ko
    ===INSMOD===   insmod_rc=0
    ===DEBUGFS===  -r--r--r-- 1 root root 0 tables ; cat_rc=0 ; 2542 bytes
    ===RMMOD===    rmmod_rc=0
    ===AFTER===    Thu Oct  1 09:23:17 UTC 2026, up 11:29

`/sys/kernel/debug/calread/` is gone again after `rmmod` (`ls: ... No such file or directory`).

### Raw dmesg from the load (verbatim, `build/register-dumps/calread_dmesg.txt`)

```
[41367.689203] omo-calread: BAR0 base=0x40000000 mapped +0x1b2f00 (576 bytes), read-only, no claim
[41367.697931] omo-calread: block first words: 00090005 00038240 0003a619 ffa60000
[41367.705225] omo-calread: 2g_power_param[26] @+0x24: 17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
[41367.730878] omo-calread: 5g_power_param[18] @+0x1ea: 00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08 001a1617 1a151813 151b1600 0000001a
```

### debugfs output, `/sys/kernel/debug/calread/tables` (2542 bytes)

```
calread: WR3000 V2.0 calibration block (read-only)
block: BAR0+0x1b2f00 len=576 (0x240)
-- raw 576-byte block --
0000: 05 00 09 00 40 82 03 00 19 a6 03 00 00 00 a6 ff
0010: 74 ff 00 00 00 00 60 ff c0 fe 00 00 17 0e 06 00
0020: 00 00 00 00 05 16 16 17 05 16 16 17 05 16 16 17
0030: 05 15 16 17 05 15 16 17 01 14 16 15 01 14 16 15
0040: 01 14 16 15 01 14 16 15 00 14 16 15 00 12 14 08
0050: 00 12 14 08 00 12 14 08 03 11 11 0c 03 11 11 0c
0060: 03 11 11 0c 02 10 10 0b 02 10 10 0b 02 10 10 0b
0070: 02 10 10 0b 01 10 10 0b 01 0f 0f 0a 01 0f 0f 0a
0080: ff 06 06 0a ff 06 06 0a ff 06 06 0a 14 08 08 08
0090: 14 08 08 08 14 08 08 08 14 07 08 08 14 07 08 08
00a0: 13 06 08 08 13 06 08 08 13 06 08 08 13 06 08 08
00b0: 12 06 08 08 12 06 08 08 12 06 08 08 12 06 08 08
00c0: 14 08 08 0c 14 08 08 0c 14 08 08 0c 13 07 07 0b
00d0: 13 07 07 0b 12 07 07 0b 12 07 07 0b 11 07 07 0b
00e0: 11 06 06 0a 11 06 06 0a 10 06 06 0a 10 06 06 0a
00f0: 10 06 06 0a 05 00 32 00 01 01 01 00 00 00 00 00
0100: aa 00 56 18 42 18 2e 18 56 18 42 18 38 18 d0 07
0110: d0 07 d0 07 d0 07 d0 07 d0 07 08 08 08 08 08 08
0120: 00 00 83 00 03 03 03 03 03 03 03 03 03 00 03 03
0130: 03 00 ff 01 ff 01 05 00 00 00 00 00 92 04 42 78
0140: d7 42 f0 2d 6a 04 ef 0a b4 17 04 39 02 04 47 f7
0150: 0c 0e 58 1f 8f 01 7f fb d2 0b 39 20 42 01 b5 fb
0160: c7 0b d2 22 2e 02 28 fa bf 0c 21 1f 7d 06 dc f2
0170: 8d 10 39 1c a7 04 00 f6 f1 0e 4e 1c b3 05 00 f4
0180: 27 10 87 1a be 05 91 f3 b2 10 c0 17 7c 08 e1 ef
0190: dd 11 cb 19 f0 06 60 f2 95 10 aa 1a c7 07 da f1
01a0: 63 10 ff 1b 0d 08 df f0 2f 11 5c 1a 00 00 1d 38
01b0: 11 f1 09 43 00 00 49 1d 13 0f 78 3c 00 00 10 27
01c0: 00 00 10 27 c5 00 ca 00 0c a9 03 00 8e e1 03 00
01d0: 00 00 1a ff 94 fd e0 fc 00 00 a6 ff 24 ff ac fe
01e0: 17 0f 07 02 00 00 00 00 05 00 00 00 00 00 00 ff
01f0: 04 00 0b 00 01 08 00 0b 00 ff 02 fd 09 00 04 09
0200: 00 09 00 fe 09 00 09 00 03 0c 04 09 00 00 00 00
0210: 00 00 0f 14 0f 16 00 05 0c 08 16 00 06 0c 08 1a
0220: 00 0f 17 16 1a 00 13 18 15 1a 00 16 1b 15 1a 00
0230: 00 00 00 00 00 ff 0c 00 0a 00 01 08 00 0a 00 fd
-- 2g_power_param: 26 words @ block+0x24 --
17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
-- 5g_power_param: 18 words @ block+0x1ea --
00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08 001a1617 1a151813 151b1600 0000001a
```

### Vendor answers (verbatim)

```
Hisilicon0  alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
Hisilicon0  alg:[SUCC]00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08 001a1617 1a151813 151b1600 0000001a
```

## Verification: word for word

Extraction: the module's words come from the line after the `-- 2g_power_param:` / `-- 5g_power_param:`
headers in the debugfs dump; the vendor's words are everything after `[SUCC]` in the `iwpriv` output.
Both sides are whitespace-normalized to one word per line. `diff` reports no difference, and the two
normalized 44-word lists have the **same md5** `d44b0f1a11876c52f47931c29fbbe8a1`.

### 2.4 GHz table (26 words)
| # | calread.ko (module) | iwpriv vendor | match |
| ---: | --- | --- | :---: |
| 1 | `17161605` | `17161605` | yes |
| 2 | `17161605` | `17161605` | yes |
| 3 | `17161605` | `17161605` | yes |
| 4 | `17161505` | `17161505` | yes |
| 5 | `17161505` | `17161505` | yes |
| 6 | `15161401` | `15161401` | yes |
| 7 | `15161401` | `15161401` | yes |
| 8 | `15161401` | `15161401` | yes |
| 9 | `15161401` | `15161401` | yes |
| 10 | `15161400` | `15161400` | yes |
| 11 | `08141200` | `08141200` | yes |
| 12 | `08141200` | `08141200` | yes |
| 13 | `08141200` | `08141200` | yes |
| 14 | `0c111103` | `0c111103` | yes |
| 15 | `0c111103` | `0c111103` | yes |
| 16 | `0c111103` | `0c111103` | yes |
| 17 | `0b101002` | `0b101002` | yes |
| 18 | `0b101002` | `0b101002` | yes |
| 19 | `0b101002` | `0b101002` | yes |
| 20 | `0b101002` | `0b101002` | yes |
| 21 | `0b101001` | `0b101001` | yes |
| 22 | `0a0f0f01` | `0a0f0f01` | yes |
| 23 | `0a0f0f01` | `0a0f0f01` | yes |
| 24 | `0a0606ff` | `0a0606ff` | yes |
| 25 | `0a0606ff` | `0a0606ff` | yes |
| 26 | `0a0606ff` | `0a0606ff` | yes |

### 5 GHz table (18 words)
| # | calread.ko (module) | iwpriv vendor | match |
| ---: | --- | --- | :---: |
| 1 | `00000000` | `00000000` | yes |
| 2 | `0004ff00` | `0004ff00` | yes |
| 3 | `0801000b` | `0801000b` | yes |
| 4 | `ff000b00` | `ff000b00` | yes |
| 5 | `0009fd02` | `0009fd02` | yes |
| 6 | `09000904` | `09000904` | yes |
| 7 | `0009fe00` | `0009fe00` | yes |
| 8 | `0c030009` | `0c030009` | yes |
| 9 | `00000904` | `00000904` | yes |
| 10 | `00000000` | `00000000` | yes |
| 11 | `160f140f` | `160f140f` | yes |
| 12 | `080c0500` | `080c0500` | yes |
| 13 | `0c060016` | `0c060016` | yes |
| 14 | `0f001a08` | `0f001a08` | yes |
| 15 | `001a1617` | `001a1617` | yes |
| 16 | `1a151813` | `1a151813` | yes |
| 17 | `151b1600` | `151b1600` | yes |
| 18 | `0000001a` | `0000001a` | yes |

**All 26 + 18 = 44 words match exactly.**

## Pulled artifacts (device md5 = local md5)

| file under `build/register-dumps/` | bytes | md5 |
| --- | ---: | --- |
| `calread_tables.txt` | 2542 | `443523eb94a7ec9565f8c07692aac8d7` |
| `calread_iwpriv_2g.txt` | 256 | `d407547707c455bc236f8b0b2a116ce7` |
| `calread_iwpriv_5g.txt` | 184 | `b6a1b8bf8a56d78dbf844384cd4f7d3d` |
| `calread_dmesg.txt` | 689 | `d22ef35fe4a1a9253532874f2fd5edac` |

sha256 of `calread_tables.txt`:
`5eac5b9441f57ac067b11a40b71779a3e582a870a4e79ab353b3c1d4c1d5038b`.

## Limits, stated plainly

- **Read-only, no claim.** No `pci_request_region`, no `pci_enable_device`, no config-space writes,
  no reset. No vendor module was loaded or unloaded by us.
- **One snapshot at load.** The debugfs text and the dmesg summary are the single copy taken during
  `module_init`; the vendor `iwpriv` answers were read after the load. They agreed. The 576-byte block
  is byte-identical across a reboot (phase 7), so the static image copy and the live WRAM answer are
  the same bytes.
- **Why BAR0 works.** BAR0 offset X equals the chip's CPU address X (phase 11); `0x1b2f00` is the
  WRAM calibration block, not a register window, so a plain memory read is all that is needed.
- **Values, not semantics.** This module returns the words; it does not claim to interpret the
  per-rate/per-chain meaning of the tables (that is phase 2 / phase 6 work).

## Reproduce

    # build + CI
    gh run download 36837345627 -n calread-ko -D /tmp/calread-ko

    # device (read-only apart from insmod/rmmod of our own module)
    scp -O /tmp/calread-ko/calread.ko root@192.168.10.1:/tmp/
    ssh root@192.168.10.1 'insmod /tmp/calread.ko; \
        cat /sys/kernel/debug/calread/tables; \
        iwpriv Hisilicon0 alg get_2g_power_param; \
        iwpriv Hisilicon0 alg get_5g_power_param; \
        dmesg | grep omo-calread; rmmod calread'
