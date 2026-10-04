# VENDOR-LOADER BOOT: verdict REJECTED - the vendor loader does NOT validate, the chip fails at runtime (phase 49, 2026-10-04)

Task 12 of the wifi-forward plan, run only after GATE G4 authorized it: substitute the scratch blob into
the LIVE vendor firmware file, boot normally, and let the vendor stack load it. The plan's branch table had
two rows: samples match -> the loader accepted a modified blob (lane open); mismatch or Wi-Fi fails to come
up -> the loader validates (lane dead, revert). What happened is the second row for outcome but the first
row for mechanism - a split the report records rather than forcing into one cell. Evidence
`build/register-dumps/exp/20261004-165759-vendorloader/`.

## G4 authorization (recorded before the run)

`.omo/ulw-execute/ledger.jsonl:73` (`event:gate`, `gate:G4`, 2026-10-04T16:55:58.647Z): "USER AUTHORIZED
on preconditions; preconditions hold (BRANCH-1 + 9/9 + healthy recovery); task 12 dispatched". The
preconditions were confirmed by this worker: task 11 landed BRANCH-1 with the `9/9` line and a healthy
recovery path (`build/register-dumps/exp/20261004-165139/acceptance.txt` -> `ALL_OK`).

## Steps (all raw outputs named)

1. backup: `backup-step.txt` -> `/root/FIRMWARE.bin.stock-20261004-165759`, md5
   `0e530b976d5a20e87358671f1a577695`.
2. substitute: `substitute-step.txt` -> live md5 `b08699bd902d35e697e24b94edb6fa2c` (== scratch.bin), sync.
3. boot: `reboot-poll.log` -> fresh boot_id `b795f917-e854-4199-8e2d-248d02f1a9f9`; Wi-Fi NEVER up.
4. measurement: `readback-post-substitution.txt`, `padding-extent-probe.txt`.
5. failure: `substituted-boot-dmesg.txt`.
6. recovery: `recovery.log`, `post-recovery-health.txt`; `final-verification.txt`.

## What the substituted boot did (verbatim, kernel timestamps)

```
[   14.007145]  version check skip
[   14.010354]  download firmware file buf len is [524288]
[   14.015527]  path=/lib/firmware/hi_wifi/FIRMWARE.bin
[   14.285804]  firmware_download success
[   34.399308]  multi_chip_loading::device_ready timeout plat_ready[0x1] device_ready[0x0]
[   34.460380]  [WIFI_MAIN]multi_chip_loading failed return error code 35629
[   34.460385]  [WIFI_MAIN]host_main_init: host_module_init return error code: -1
[   81.519735]  Enter host_plat_panic_msg_process!!!, chip:0
[   81.525050]  chip[0] excp_type:[0](0:EXCP_PANIC,1:EXCP_HEART,2:EXCP_CHANNEL,3:EXCP_WIFI)
[   81.538542]  DMAC:us_fault_type_bitmap:[0x10]->[EXCP_DABT]
```

`build/register-dumps/exp/20261004-165759-vendorloader/substituted-boot-dmesg.txt`, quoted in
`interp.txt`. `hi5622v100_wifi` never loads (lsmod: only `hi5622v100_plat`), `iw dev` = 0 interfaces for
the full 300 s window. The failure signature is the CHIP never reaching `device_ready` (34.4 s timeout,
code 35629) then a chip data abort at 81.5 s. `/sys/fs/pstore` is UNCHANGED (the host kernel did not
panic); the chip dumps landed in `/log/wifi/excp_*.bin` (`final-verification.txt` lists the three).

## The decisive reads: the loader accepted OUR bytes

`readback-post-substitution.txt` compares the loaded region against the stock file and the scratch file:

| site | host addr | loaded value | stock file | scratch file | matches |
| --- | --- | --- | --- | --- | --- |
| file `0x6ed4` | `0x406FEED4` | `0xF8E8F0C1` | `0xA000F8C7` | `0xF8E8F0C1` | SCRATCH |
| file `0x87024` | `0x4077F024` | `0xF8B8F041` | `0x4B07BF38` | `0xF8B8F041` | SCRATCH |
| S1 | `0x407BB5A0` | `0x00001000` | `0x00000000` | `0x00001000` | SCRATCH |
| S1+4 | `0x407BB5A4` | `0x40161108` | `0x00000000` | `0x40161108` | SCRATCH |
| S2 | `0x407BBEB4` | `0x00000001` | `0x00000000` | `0x00000001` | SCRATCH |
| S2+4 | `0x407BBEB8` | `0x50AA7E49` | `0x00104427` | `0x50AA7E49` | SCRATCH |

Both patch sites carry the scratch bytes, and the four S1/S2 cells are written - and those cells are
written ONLY by the scratch trampolines, so the patched CODE executed, not just the DATA. The BAR0 mirrors
of the four cells read identically, `0x406B8000 = 0xE59FF018` (window decoded), and the on-device md5 of
the live file at capture was `b08699bd902d35e697e24b94edb6fa2c` (== scratch.bin). The trampoline padding
also loaded (`padding-extent-probe.txt`: `0x406F8000+0xc80a8 = 0xF643B43F`, `+0xc8198 = 0x8C00F3EF`,
`+0xc819c = 0xF241B43F` == scratch.bin at those file offsets). The load is not truncated: stock bytes still
match at file `0xce000`/`0xcf000`.

## The verdict, split along the plan's two rows

- **Outcome row** ("Wi-Fi fails to come up -> lane dead, revert"): matched. The vendor-loader lane is DEAD
  AS-IS for this patch. Do not use it.
- **Mechanism row** ("mismatch -> the loader validates at a layer the static scan missed"): FALSIFIED. The
  loader logs "version check skip", downloads the file, and the loaded region contains OUR bytes at both
  patch sites with our cells written. The fence is downstream, at FIRMWARE RUNTIME: the chip never reaches
  `device_ready` (34.4 s) and then data-aborts (81.5 s). So the positive finding is the narrower claim
  "the vendor loader performs no blob validation - a modified blob does reach the chip".

## Residual ambiguity (declared, not forced into a mechanism)

The reads do NOT settle WHICH aspect of the patch kills the chip: the S1/S2 stores landing in live
vendor-firmware memory, OR a trampoline register/flag-liveness defect in a code path the takeover boot
never exercised, are both consistent with the evidence (`interp.txt`, RESIDUAL AMBIGUITY). No further
experiment was run; the plan says revert.

## The last-16-bytes read was NOT a discriminator (recorded so others need not rediscover it)

The plan-requested "loaded-image first/last 16 bytes" read (`interp.txt`, APPENDIX): first 16 bytes @
`0x406F8000` read `0x00046971 0x000C742D ...` in both boots (word 2 changed `0x0 -> 0x10`, a runtime write);
last 16 bytes @ `0x407DAC88..0x407DAC94` are `0x00000000 x4` in BOTH boots while the file's last 16 bytes
are `46030000 8d060000 1b0d0000 361a0000`, and `0x407D8000` diverges from the file. So the window's file
identity does not extend to the file tail; the decisive compares are the two patch sites, the four S1/S2
cells, and the two trampoline padding blocks - all inside the verified-loaded region.

## Recovery (ran in ALL outcomes)

`recovery.log`: restore the backup, md5 re-verified `0e530b976d5a20e87358671f1a577695` BEFORE rebooting
(`RESTORE_OK`); `/tmp/FIRMWARE.bin.scratch` removed; sync; reboot -> fresh boot
`b6e31815-57e9-4015-b738-8abeb03fc151`; `post-recovery-health.txt` `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1
OMO_OFF=0 OMOPAT=0 WIFIDRV1=0 LOADERS=0 PSTORE=3`, with `STOCKMD5=0e530b976d5a20e87358671f1a577695`
from `recovery.log` (the line is synthesized from both files). The recovered
boot's own dmesg reads `multi_chip_loading SUCC!` vs the failed boot's `... failed return error code
35629`. Discriminator cells re-read after recovery are back to the STOCK baseline
(`0x406FEED4=0xA000F8C7`, `0x4077F024=0x4B07BF38`, S1/S1+4/S2 = `0/0/0`, S2+4 = `0x00104427`).
`final-verification.txt`: live md5 stock; no `.omo-pat` leftover; backup present; 6 interfaces; 2 wiphys;
no `.omo-off`; `CAL2G_OK=1`.
