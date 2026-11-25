# TABLES - wifi-5ghz-bringup (task-154)

All facts are read-only RE/ledger evidence or host test output. Addresses are
device CAs only; no vendor byte, firmware segment or calibration value is
reproduced here.

## T1 - endpoints: two host views of ONE chip

| endpoint | PCI domain | host BAR0 | host BAR2 | cfg[0xff8] | phy_devid | INTx | vendor role |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0000:00:00.0 | 0 | 0x40000000 | 0x41800000 | 0x00011521 | 1 | 207 (0 deliveries) | our boot/service window |
| 0001:00:00.0 | 1 | 0x58000000 | 0x59800000 | 0x00011520 | 0 | 209 (live ISR) | vendor message/IRQ path (chip->dev[0]) |

Evidence: phase21/sibling-ep.md:90-91 (both print chip 0), phase21/sibling-ep.md
A.3/A.4, phase32/doorbell-endpoint-path.md:18-21.

| claim | status |
| --- | --- |
| both functions = the same chip 0 | [V] vendor live log |
| two register instances 0x800 apart; vendor initialises only instance 0 | [V] phase21:155,195 |
| BAR0 windows alias the same on-chip register space | [V] phase21:198 (identical CA reads) |
| one firmware image / one HCC service instance | [V] transport lane + phase21 A.5 |
| a second boot ladder on 0001 is a second write path to the SAME service (unsafe) | [V] by the alias; the class of failure is recorded (chip stuck exception) |

## T2 - wiphy band plan (this lane's fix)

| band5g | endpoint domain | wiphy bands | wiphy count | iw expectation |
| --- | --- | --- | --- | --- |
| 0 (default) | 0 | 2.4 GHz only | 1 registered | phy0 2.4 GHz (validated behaviour) |
| 0 (default) | 1 | 5 GHz only | allocated, NOT registered (no service) | not visible |
| 1 (this lane) | 0 | **2.4 GHz + 5 GHz** | **1 registered** | phy0 with two bands |
| 1 (this lane) | 1 | none | 0 | dmesg "SECONDARY host view ... no wiphy" |

Reason: one chip = one radio = one wiphy with two bands (the vendor splits it
per band in software only); a per-endpoint second wiphy would have no booted
service and no D2H routing.

## T3 - wiphy registration gate (unchanged by this lane)

| field | producer | scope |
| --- | --- | --- |
| boot_latched | golden handshake page word (+8 == 2) once | per service ctx |
| phase >= READY_TXQ_SYNC | g4 id1 plat-ready + g4 id2 dev-ready on that ctx's D2H path | per service ctx |
| plat_ready / dev_ready | g4 id1 / g4 id2 decode | per service ctx |
| register action | hi5622_cfg_reg_gate() -> wiphy_register() work item | per wiphy |

The gate is correct and unchanged: the fix makes the ONE booted service own
the ONE radio's both-band wiphy instead of asking a never-booted endpoint to
register a second one.

## T4 - band bytes on the AP/scan wire (what the fix derives differently)

| message | field | pre-fix source | post-fix source | 2.4 GHz | 5 GHz ch36 |
| --- | --- | --- | --- | --- | --- |
| AP VAP add (g0 id7 base) | band | c->band (wiphy legacy byte) | p->band (start_ap chandef) | 0 | 1 |
| AP VAP base / VAP start | band | c->band | p->band | 0 | 1 |
| AP channel config 0x0124 | band | p->band | p->band (unchanged) | 0 | 1 |
| H1 band config g0 id10 | channel | already: IEEE channel number from freq (0001) | unchanged | 1..14 | 36 |
| scan request g0 id0xC | chip_id/band | c->band | band of the requested channels (hi5622_band5g_freq_band) | 0 | 1 |
| interface user_add (creation) | band | c->band | c->band (no channel yet; [U]) | 0 | 0 |

## T5 - artifact / verification

| item | value |
| --- | --- |
| base module under test | association module, stripped 344,372 B sha256 ab9e18a4..., 56 params |
| this lane module | stripped 351,008 B sha256 13366151..., unstripped 1,342,188 B, 57 params |
| .text sha256 | b21bbc32... (debug-strip identical; two builds byte-identical) |
| vermagic / depends | 6.18.60 SMP mod_unload ARMv7 p2v8 / cfg80211 |
| build warnings | 0 |
| portable selftest | 114 checks, 0 failures |
| patch static checks | 20 checks, 0 failures |
| build host | opensource target toolchain arm-openwrt-linux-muslgnueabi-, kernel tree 6.18.60 |

## T6 - fire-gated unknowns

| # | unknown | first observation |
| --- | --- | --- |
| U1 | 5 GHz regulatory: world regdomain NO_IR -> needs a country domain | G0 iw reg get/set |
| U2 | firmware acceptance of user_add band 0 + AP VAP add band 1 | G5 dmesg |
| U3 | firmware 5 GHz beacon/AP worker behaviour on a dual-band wiphy | G5/G6 page + observer |
| U4 | on-air 5 GHz beacon with the attached antennas | G6 scan/monitor |
