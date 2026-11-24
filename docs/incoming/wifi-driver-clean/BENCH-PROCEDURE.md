# BENCH-PROCEDURE - clean-room Hi5622V100 Wi-Fi driver (attended)

Host-only lane deliverable; this file is the ladder for the Lead's attended
session.  Stop rules are unchanged from the project: recovery door armed first,
UART read-only, one idea per session, capture-and-stop on abort/SError/hang, no
retries in-session.  This driver never contacts the router by itself.

## Preconditions

* recovery door armed, bench link PASS, UART read-only capture to an absolute path;
* image + module hashes recorded (see BUILD-NOTES.md for the source build);
* local firmware staged in /lib/firmware on the device rootfs:
  - `hi_wifi/FIRMWARE.bin` (vendor image, local evidence),
  - `hi_wifi/wifi_block.bin` (0x9a8 B PHY custom block, staged with
    `tools/mkblocks.py` from the local stock capture; never committed).

## Sysfs driver path

The module creates `/sys/bus/pci/devices/<bdf>/` attributes (each subsystem
owns its group; the loader appends the PCI function name):

    transport: prepare upload verify seed release gate_repair status boot dev_status
    service:   hcc_arm hcc_loop hcc_service hcc_clear hcc_status hcc_ready
               hcc_events hcc_rings hcc_plan
    hcc:       hcc_sr_announce hcc_cece_kick hcc_dr_republish
    d2h:       rd_poll rd_advance rd_status rd_decode rd_catalog

## Ladder

| step | action | expected / stop rule |
| --- | --- | --- |
| S0 | `insmod hi5622_wifi.ko` | probe only: no device writes; ring mirrors in host DRAM; "HCC service ready" + "HCC core: portable hcc_core initialised" |
| S1 | `echo 1 > prepare` | six endpoint-iATU viewports, 6/6 readback=OK (ROM window now full 1.75 MiB) |
| S2 | `echo 1 > upload ; echo 1 > verify` | upload 928,920 B via firmware loader; verify diffs=0 |
| S3 | `echo 1 > seed` | platform descriptor (built-in INI values, 24 words) + wifi block (local file, 618 words) written and read back; STOP if readback diffs |
| S4 | `echo 1 > hcc_arm` | vendor-exact 7-channel ETE init (intr RMW first, DR free buffers pre-posted), readback OK. NEVER write CECE here |
| S5 | `echo 1 > hcc_loop` | service thread starts (core tick + direct reader primary). Start BEFORE the release |
| S6 | `echo 1 > release` | 0x5A5A ONLY; refused unless windows+upload+verify+seed are done; the transport records the release with the core (latched boot watch) |
| S7 | watch `cat boot hcc_ready hcc_status hcc_events rd_status` | LATCHED handshake 0x86807fef/7/2/0xabcd (page+8==2), state CA 0x118b60=1, mask CA 0x40008=0; direct reader decodes g4 id1 <=2.0 s then g4 id2 <=20.0 s (phase WAIT_DEVICE_READY -> READY_TXQ_SYNC) |
| S8 | optional liveness | `printf 1 > hcc_sr_announce` (valid group-0 id-0 frame, coalesced mailbox id3; ONE frame per module load). Consumed => H2D consumer alive; out[0] bit3 stuck => core closes the H2D lane, stop |
| S9 | optional nudge | `echo 1 > hcc_dr_republish` (vendor-owned DR +0x38 only, one per session) |

Anything with a data abort / SError / wedged read: capture dmesg + UART, then
power-cycle.  Do not rmmod after the release (the vendor never unloads).

## Read-out cheat sheet

* `boot`: handshake page + gate (16-bit: 0xceed = ROM stage, 0xcece = image
  post-boot) + state/mask/boot slot + mailbox words (out[5] is read-only).
* `hcc_ready` / `hcc_events` / `hcc_status`: the hcc_core status line
  (phase, latched boot, direct/engine ready counters, D2H/h2d/cece/safety
  counters) and the direct-reader catalogue line.
* `rd_catalog` / `rd_decode`: device->host frames decoded directly from the
  device group-A rings (works with the ETE engine idle).
