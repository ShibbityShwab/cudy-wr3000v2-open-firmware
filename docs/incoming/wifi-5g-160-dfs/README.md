# wifi-5g-160-dfs - the 160 MHz and DFS/CAC machinery for the 5 GHz AP

**Lane:** shared task **task-205**. **HOST-ONLY**: static decode of the pinned
local vendor artifacts; no device contact, no UART, no fire, no flash.
**The Lead fires.** No vendor binary, firmware byte, calibration value, MAC or
secret is stored in this lane; the tool reads the pinned local artifacts at run
time and prints derived values only.

## THE DEPENDENCY (read this first)

**Both features are downstream of the 5 GHz band-1 MAC start, and the band-1
MAC start is still open.** Today the 5 GHz RF is programmed (the F2 fire, mode 3
band 1, the ch36-64 RF image; the beacon state is present) but no 5 GHz beacon
is on air because the band-1 MAC/TBTT arm was never posted: our AP-start ladder
posts 0x0064/0x0065/0x0124/0x0127/0x01F7/0x00F7 and never the band-1 start.
The sibling lanes decode the missing rung: **H2D g0 id1 key 0x0242**
(worker CA 0x4C524; body = {01,00,01,00,00}; field B = a fresh band-1 module
context created by key 0x0064 with data[0x12]=1) - see
../wifi-band1-start/README.md (task-197) and the
wifi-band1-start-send implementation lane (task-206).

Consequence for this lane: **the 160 MHz and DFS fire recipes below are
GATED.** Do not run any rung of either recipe until the band-1 start rung is
proven by the on-air gate (an observer scan on the 5 GHz channel finds the
SSID). The decode, the probes and the recipes are ready for that moment; a
160 MHz or DFS AP start before the band-1 MAC start would test nothing (the
firmware RF programme and the beacon engine would still be unarmed) and is
forbidden by this recipe.

## The one-paragraph result

**160 MHz:** the capability side is already DONE in the driver (vendor wiphy
VHT cap 0x00400064 = HTC_VHT | SUPP_CHAN_WIDTH_160MHZ | SGI80 | SGI160; the
image #3 module advertises it on the 5 GHz band), and cfg80211's 160 MHz
chandef check passes. The missing piece is the **width selector**: WAL/H2D
channel config event **0x0124** carries a 4-byte body **{band, bandwidth,
channel, 0}** - byte 1 is a bandwidth enum, NOT a VAP index. The firmware
worker CA 0x841B8 stores it to module ctx+0x12; the 5 GHz RF branch CA 0x40684
then calls **CA 0x101890(band, field=ctx+0x12, channel)**, which selects a
per-bandwidth descriptor window from the parameter table CA 0x10BE68 and only
then lets CA 0xA2FA2 (and its ch36-64 image builder CA 0x89924) programme the
RF block. So 80/160 MHz is chosen by 0x0124 body[1] before the RF image; the
earlier CA 0x89924 reading has no width argument of its own.
**DFS/CAC:** the vendor host drives CAC with H2D key **0x07E7**, a 1-byte body
**{1} = CAC/radar-detect arm / {0} = stop**, plus H2D key 0x0136 (radartool
string) for tuning. The CAC default is **60000 ms** (TDWR/weather band
5600-5650 MHz uses **600000 ms**), run as 63 s host timer ticks; the firmware
arms six radar-detect registers on {1}. CAC completion sends {0}, iterates the
channel list and calls the band start (CA 0xAD22C/0xACAEC report to the host);
a radar hit arrives as a D2H event handled by hmac_dfs_radar_detect_event
(CA 0xAC7C0), which feeds the AP FSM event 4/arg 2. The init'able UNII-2/2e
channels are 52-64 and 100-144 (the firmware band-1 table has 29 channels,
including ch144); our driver currently advertises 36-48/52-64/100-140 and must
add ch144 for the full DFS set.

## Files

| file | content |
| --- | --- |
| REPORT-160MHZ.md | the 160 MHz decode: capability word, cfg80211 translation, 0x0124 body, CA 0x101890/CA 0xA2FA2 width path, CCFS layout, driver+hostapd changes, open items |
| REPORT-DFS.md | the DFS/CAC decode: 0x07E7 state machine, timing constants, radar D2H handler, DFS domain/channel flags, cfg80211/hostapd path, open items |
| TABLES.md | every address, key, field, frame layout, timing constant and channel table |
| FIRE-RECIPES.md | the GATED rungs for both features: probes, expected observables, PASS/FAIL/abort, stop rules |
| HOST-IMPLEMENTATION.md | the exact driver changes (0x0124 body[1] selector, radar-detect op, CAC timer, ch144) and the hostapd config |
| PROBES.md | read-only probes (hostapd/iw/observer + BAR0/CA object reads) for when the beacon lands |
| hostapd/ | the two AP config fixtures (160 MHz ch36; DFS ch52) |
| tools/fw_5g_160_dfs_re.py | reproducible decode/assert tool (reads the pinned local artifacts in place; --selftest, --tables, --frames) |
| evidence/ | captured tool outputs (derived values only) |

## Reproduce (host only)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    $PY tools/fw_5g_160_dfs_re.py --selftest
    $PY tools/fw_5g_160_dfs_re.py --tables
    $PY tools/fw_5g_160_dfs_re.py --frames

Pinned local artifacts (never committed): the firmware image 928,920 B sha256
7fc87e20...311b (CPU = file + 0x40000) and the vendor host module 3,581,748 B
sha256 22fa789a...c7d2. The tool shells out to the lab objdump at
/home/shibbityshwab/openwrt-lab/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump
and fails closed if it is missing (the pinned pins and table decodes still run).

## Standing rules

Every rung requires the bench protocol: recovery armed, UART read-only, one AP
per load, power-cycle on the exception mask/page change, never UART transmit,
never write CA 0x400392F0, never read the RC misc window 0x10160000-0x1016FFFF,
never rmmod the vendor modules. The 5 GHz beacon TX (band-1 MAC start) is the
precondition for every rung below; until it lands, this lane is documentation
only.
