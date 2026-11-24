# wifi-5ghz - 5 GHz band enable (band config + per-band wiphy + hostapd a)

**Lane:** `wifi-5ghz` (shared task **task-145**), host-only clean-room.
**Written:** 2026-11-24. **Status:** static RE complete; band5g patch + host
tests + compile-verify delivered; **bench ladder designed, NOT fired** (the
Lead fires, attended, recovery armed).

This directory is the lane's deliverable on top of the hardware-validated
FINAL ALL-PATCH module (`opensource` 008aca5; stripped 308,080 B sha256
`5b587949...`; 47 params).  It adds the 5 GHz band-enable parameter, the
vendor 5 GHz wiphy profile, the band-aware AP-start / H1 band-config plumbing,
and a 5 GHz AP-start + observer-scan ladder.

Everything here is **HOST-ONLY**: no device contact, no firmware write, no
vendor binary/source/calibration byte in any tracked file.  The vendor modules
and firmware are referenced by path only and remain local/gitignored.

---

## TL;DR

1. The vendor's 5 GHz bring-up is a **per-band wiphy profile + a band byte in
   the existing AP-start channel message** - there is **no separate 5 GHz
   bring-up message** and no host beacon template.  The firmware owns the
   5 GHz path (`smac_tbtt_5g_isr`) and selects it from the band/channel in
   the module channel config (WAL 0x0124 body `{band, vap, channel, 0}`) and
   the H1 band config (g0 id10).
2. Both the host module and the firmware carry the **same two static band
   tables**: 2.4 GHz **14 channels** (ch1..14) and 5 GHz **29 channels**
   (36..64, 100..144, 149..165, and the 4.9 GHz block 184..196 at
   4920..4980 MHz).  The host tables are `g_ast_channel_list_2G`/`_5G` +
   `g_ast_freq_map_2g`/`_5g`; the firmware tables are at CPU addresses
   0x117B48 / 0x117AD4.
3. The **H1 g0 id10 channel field is the IEEE channel NUMBER**, resolved by
   the firmware chip channel table (`CA 0xD6A1E` -> `0x94728` ->
   `{count, stride, base}` + valid bitmap).  The former [U] "5 GHz firmware
   channel-id table" does not exist; the old clean-room auto path sent the raw
   MHz value (e.g. 5180) and could never resolve.
4. The **scan-request channel index** must be the vendor table position
   (0..13 / 0..28); the previous 19-channel 5 GHz list is wrong after ch140
   and omits ch144/149..165/4.9 GHz entirely.  Fixed here.
5. Our driver's **domain 0 -> 2.4 GHz / domain 1 -> 5 GHz** mapping remains
   **[I]** - the vendor selects the band by user-space command name
   (`g2`/`g5`) and maps band type 0/1 -> band id -> wiphy, not by PCI
   domain.  The ladder therefore identifies the 5 GHz wiphy with `iw phy` and
   keeps `band_force`/`band5g` as the controls.
6. Delivered: `band5g` module parameter (default 0 = bench-validated
   behaviour, 5 GHz AP start **fail-closed**), a 29-channel/HT 0x62/VHT
   0x400064 5 GHz wiphy profile, vendor-table validation for the AP channel
   config and the H1 band config, a H1/scan-index fix, hostapd `hw_mode=a`
   configs, 98 host checks, 73 static-RE probes (2 module builds + firmware),
   zero-warning compile verify (312,432 B stripped artifact, 48 params), and a
   no-fire ladder including the on-air observer scan.

---

## File map

| path | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | the reversal: dual-band model, tables, channel-index semantics, AP/H1 5 GHz flow, implementation, verification, limits |
| [TABLES.md](TABLES.md) | T1-T11: addresses, channel lists, capability words, payload maps, evidence counts |
| [TEST-LADDER.md](TEST-LADDER.md) | G0-G6: 5 GHz AP start + observer scan, stop rules (not fired) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | reproduce the host build + evidence, artifact identity |
| [src/hi5622_band5g.c](src/hi5622_band5g.c), [src/hi5622_band5g.h](src/hi5622_band5g.h) | the portable band table/band-config logic (new driver files) |
| [src/band5g_selftest.c](src/band5g_selftest.c) | 98 host checks |
| [patches/0001-band5g.patch](patches/0001-band5g.patch) | applies on the FINAL ALL-PATCH tree (`patch -p1`): new files + cfg80211/scan/vapscan_map/Kbuild |
| [tools/band5g_re.py](tools/band5g_re.py) | re-derives every static claim from the local vendor artifacts |
| [tools/build-band5g.sh](tools/build-band5g.sh) | patches + builds the combined module (host-only) |
| [tools/selftest.sh](tools/selftest.sh) | host test runner |
| [ladders/](ladders/) | hostapd 5 GHz configs + observer scan helper |
| [evidence/](evidence/) | tool output, selftest, build/patch/artifact identity |

Reproduce (host-only, no bench):

    ./tools/selftest.sh
    ../../../../.provision-venv/bin/python tools/band5g_re.py \
        --wifi-ko  <local 2.5.24 hi5622v100_wifi.ko> \
        --wifi-ko-old <local 2.4.15 hi5622v100_wifi.ko> \
        --firmware <local FIRMWARE.PD4.bin>
    ./tools/build-band5g.sh        # needs the 6.18.60 kernel tree + final all-patch tree

Evidence markers: **[V]** re-verified by this lane from the named local
artifact (disassembly/data only); **[V*]** device-measured by an earlier
attended lane (cited doc); **[I]** inference; **[U]** unknown.
