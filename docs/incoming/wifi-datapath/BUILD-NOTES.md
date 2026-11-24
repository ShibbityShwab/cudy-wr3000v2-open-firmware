# BUILD-NOTES - host-only verification evidence

## 1. Frame-builder library (pure C99)

    cd opensource/docs/incoming/wifi-datapath
    ./tools/build.sh

Output on the authoring host (2026-11-24):

    == compile (-std=c99 -Wall -Wextra -Werror -pedantic) ==
    == run selftest ==
    SUMMARY: 77 passed, 0 failed
    BUILD OK (host-only; no device contact)

Compiler flags: -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow
-Wstrict-prototypes -Wmissing-prototypes. No external libraries beyond libc; the header is
kernel-safe (uses stdint/stddef only; a future kernel integration can include it in the
module sources unchanged, with memcpy/memset already provided).

Test vectors and sources:
* ctx-create 48-byte frame: the bench-validated frame of the wifi-ctx-create lane (hardware
  lap 2026-11-25 05:10); the same bytes were posted and the firmware allocated ctx+0x190.
* txq 0xd8 48-byte frame: vendor live capture (../../phase20/tx-path.md A.5, re-derived in
  ../../phase38); 16-byte run of zeros after the 4-byte data word, inherited pad ff*4.
* txq addr sync 0x320: layout asserted from hmac_txq_table_addr_sync disassembly (2.4.15 and
  2.5.24 host modules).
* refusal cases: bad tag, zero/oversized total, group > 2, alloc nibble != 1, dst mismatch,
  short buffer, oversized ctx fields, TX entry field overruns, NULL data with len != 0.

## 2. Firmware key-table decoder

    .provision-venv/bin/python tools/fw_dp_tables.py --selftest
    SUMMARY: 35 passed, 0 failed

Checks: pinned image sha256 7fc87e20...311b and 928,920 B; table A CA 0x10c23c with 217
slots and table B CA 0x103bf8 with 88 slots; sentinel {key 0x1451, worker 0} at each end;
live workers Thumb; known rows 0x0004/0x0064/0x00d8/0x0320; every vendor wire key
(0x0064/0x0078/0x00d8/0x0128/0x0311/0x0320) resolves in A or B; table-A-only membership of
0x0064; unique keys.

    .provision-venv/bin/python tools/fw_dp_tables.py --emit-md tables/firmware-config-keys.md

regenerates the committed decoded table (324 lines). The tool reads the local firmware blob
in place and prints/derives annotations; no vendor bytes are copied into the repository.

## 3. What was NOT built or run

* No device contact of any kind by this lane (all bench statements are cited from committed
  lanes/ledger).
* No kernel/module build was attempted: the library is standalone C; a later driver lane
  should compile src/dp_frames.c into the module and re-run this selftest (the source uses
  only memcpy/memset and fixed-width types).
* No image, no flash, no vendor module load.
