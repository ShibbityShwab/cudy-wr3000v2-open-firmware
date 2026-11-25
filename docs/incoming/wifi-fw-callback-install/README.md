# wifi-fw-callback-install - who installs G+0x370 (CA 0x174B34)?

**Lane:** shared task task-186 (retry of failed task-185). **HOST-ONLY,
read-only**: no device, bench link, UART or network access, no fire.

## Verdict in one paragraph

The firmware image contains **no instruction anywhere that installs
`G+0x370`** (band-1 MAC-start callback slot). The five other slots of the
same hook block are installed at **firmware boot** by one initializer
(`CA 0x5B424`, called from the pre-HCC boot init at `CA 0x48442`, just
before the descriptor-pool default at `CA 0x485FA..0x48620`); that installer
runs on our host-only boot too (bench read `G+0x374 = 0x57639` is exactly its
literal) and skips `G+0x370` by design. VAP start-on-band `CA 0x971D8` only
*reads* `G+0x370`/`G+0x374` and safely skips a NULL. The host modules carry
neither the G address nor the callback values. **Conclusion: the NULL
`G+0x370` is not a missing boot/host step; C5a should be retired unless a
stock differential shows the slot non-NULL.** The intended callback CA cannot
be derived from the artifacts ([U]): the slot has no writer, literal or
pointer-table entry.

## Files

| file | content |
| --- | --- |
| `SEARCH-RESULTS.md` | search half (committed first): all address-construction/writer searches, the exact writer instructions, the boot call site, eliminated candidates |
| `ANALYSIS.md` | decode of the hook block, host/platform primitive survey, the `[U]` intended-CA finding, the read-only probe pack (P0-P3), the fire/no-fire recipe, and the correction to the desc-pool C5a verdict |
| `tools/fw_callback_install_re.py` | reproducible tool (selftest 20/20): whole-image search, G taint scan, hook-block read/alias/indexed analysis, callback summaries |
| `evidence/` | tool outputs (derived addresses only; no vendor bytes) |

## Reproduce

```
python3 tools/fw_callback_install_re.py --selftest    # 20 PASS, 0 FAIL
python3 tools/fw_callback_install_re.py --search      # = evidence/search.txt
python3 tools/fw_callback_install_re.py --callbacks   # = evidence/callbacks.txt
```

Artifact (local, uncommitted): `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`,
928,920 B, sha256 `7fc87e20...311b`; CPU = file offset + 0x40000;
bar0read phys = 0x40000000 + CPU + 0x6B8000.

## Boundary

No vendor binary, firmware byte, extracted table, credential or per-unit value
is committed; the tool reads the pinned local artifacts at run time. No device,
bench link, UART, network access or fire was used.
