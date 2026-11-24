# BUILD-NOTES - host-only verification evidence (wifi-datapath-bringup)

Reproduce:

    cd opensource/docs/incoming/wifi-datapath-bringup
    ./tools/build.sh

The script (1) compiles and runs the new data-plane host selftest with
`-std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes
-Wmissing-prototypes`, (2) compiles and runs the inherited H2D control-plane
regression selftest, (3) runs the static-decode selftest against the local
vendor artifacts, (4) builds the out-of-tree kernel module against the project
6.18.60 luofu tree and reports size/sha256/vermagic/symbols/warnings, (5)
validates sequences/ladder-d-datapath.json through the bench tooling's own
protocol-sequence validator, (6) source checks.

## 1. Observed output (authoring host, 2026-11-25)

    == 1. host data-plane selftest (device-free) ==
    dp_rings selftest: 69/69 checks PASS
    == 2. host H2D control-plane regression selftest ==
    h2d_send_logic selftest: 308/308 checks PASS
    == 3. static decode selftest (local vendor artifacts, host-only) ==
    SUMMARY: 34 passed, 0 failed, 0 skipped
    == 4. kernel module build (project 6.18.60 tree) ==
    module      : .../src/hi5622_wifi.ko
    size_bytes  : 462952
    sha256      : 8fc693ad4c3c9338baa1089ecb2acd420be78eb059760bfc0fa356ade205cec9
    vermagic    : 6.18.60 SMP mod_unload ARMv7 p2v8
    dp syms     : 13 (T dp_*/hi5622_dp*)
    h2d syms    : 38 (T h2d_*)
    warnings    : 0 (compiler+modpost; 0 expected)
    == 5. protocol sequence schema validation ==
    SUMMARY: 10 passed, 0 failed
    == 6. source checks ==
    OK: host data-plane selftest + H2D regression + static decode + clean kernel build

The kernel tree used was
`/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`
with its GCC 14.4 musl cross compiler. The build script falls back to other
configured luofu 6.18.60 trees if that one is cleaned; the kernel tree is a
local build product, not a deliverable.

## 2. What the 69 data-plane checks cover

ring geometry (all sizes/depths/buffer lengths/table strides), packed ring index
wrap arithmetic, ready-payload PKTRAM/ACP validation and refusals, MSDU-table
entry CA arithmetic (tid/user bounds, PKTRAM limit), 12-byte entry image, TX ring
and RX buffer device-VA arithmetic, the 14-write MAC program order/values/RMW
masks and its refusals (wrong depths, out-of-window addresses, short output), the
byte-exact 32-byte key-0x0320 frame, and the extended gate matrix
(ctx-create-required, one-shot, budget/in-flight/phase).

## 3. What the 34 static-decode checks cover

vendor module md5 pin; symbol boundaries of all ten decode targets; the 0x2524
TXQ allocation; the init order (MSDU table base before address sync); the
`hdpp_config_send_event(cmd=0x320, len=4, data=[dev+0x948])` call; the d2h-read-msg
-> ACP -> MAC +0x10 chain; the 12-byte / tid+user*8 table arithmetic and its
devca->hostva resolution; the {devVA,0,0} entry writes and publish barriers; the
0x40 TX-ring depth default; the RX ring allocation sizes/entry sizes/buffer
lengths and 12-bit size fields; tx_ba_info 0x800 B/depth 0x100; the TXQ per-user
block stride; the config-event key/len header offsets; firmware sha256/len pin and
the two firmware worker instruction patterns (0x81445/0x81039).

## 4. Not built / not run by this lane

* No device contact of any kind: no flash, no module load, no register read or
  write. Every bench statement in the docs is a plan with explicit stop rules.
* No image was built; the module is an extension of the existing clean driver
  sources copied from wifi-h2d-send (control plane unchanged except the new
  key-0x0320 message and the new sysfs group).
* No frame has ever traversed the rings; TX completion/A-MSDU/BA reordering is
  out of scope (see REPORT.md section 4).

## 5. Handoff notes

* bench-auto: the ladder uses the sysfs attrs `dp_probe, dp_setup,
  dp_txq_sync/h2d_txq_addr_sync, dp_status, dp_rings`. The bench tooling
  allowlist (bench_guards.DP_ATTRS) currently knows only `h2d_txq_addr_sync`;
  add the four dp_* names (dp_setup also to DP_WO_ATTRS) before running
  sequences/ladder-d-datapath.json. tools/validate_sequence.py prints the exact
  handoff and validates the sequence with the names injected in memory.
* cfg80211/upstream-target: the extension is deliberately sysfs-driven and
  bench-gated; it is not part of an upstream submission series (firmware is
  closed and the MAC block programming is bench instrumentation, not a driver
  data path yet).
* The module keeps the inherited module parameters (fw_path, block paths,
  h2d_send_budget, etc.). dp_setup itself consumes no H2D budget; the ladder
  needs h2d_send_budget=2 (ctx-create + the 0x0320 frame).
