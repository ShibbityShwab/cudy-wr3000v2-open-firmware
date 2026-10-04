# READ BOOT: fn_array[0x4c] is POPULATED in a takeover boot - firmware register(0x4c) ran (phase 49, 2026-10-04)

Task 9 of the wifi-forward plan: one read-only takeover boot that settles the *register* half of the
last link, plus the scratch-cell baselines task 11 needs. The port never submits a descriptor and never
rings the doorbell in this run (bounded by design); the only device work is the port's own documented
decode path. Evidence `build/register-dumps/exp/20261004-163526/`.

## The measurement

The EXP_CAPTURE_CMD hook (`capture-cmd.txt`; the verbatim hook string is `capture-cmd.hook.txt`)
performed READ-ONLY `devmem <addr> 32` after the port's `omo-drv1: init done` marker:

```
--- L1  fn_array[0x4c]  0x4017D560 (expected 0x00040295) ---
0x00040295
--- CTL fn_array[0x2d]  0x4017D4E4 (control, known 0x000462f9) ---
0x000462F9
--- S1   0x401035A0 ---  0x00000000
--- S1+4 0x401035A4 ---  0x00000000
--- S2   0x40103EB4 ---  0x00000000
--- S2+4 0x40103EB8 ---  0x00104427
--- WIN  0x406B8000 (window sanity) ---  0xE59FF018
```

`build/register-dumps/exp/20261004-163526/capture-cmd.txt`.

- **L1 = `0x00040295` at CA `0x4017D560`.** That word is the fn-pool pointer `0x00040295` the firmware
  writes into the `fn_array[0x4c]` slot when `register(0x4c)` runs (phase 47 `THE-LAST-LINK.md` section 1:
  fn pool `0x9858` = `0x00040295`). The slot is populated in this takeover boot; the "handler table
  populated but slot zero" contradiction does not exist here. The offline precheck read the same value
  from the frozen normal-op barmap before the run.
- **Control = `0x000462F9` at CA `0x4017D4E4`** (`fn_array[0x2d]`), the known non-zero neighbour slot: a
  populated sibling, so L1 is not reading a uniformly-zero table.
- **Window sanity = `0xE59FF018` at `0x406B8000`**, a decoded window word, so the reads landed through a
  live inbound decode and not through a dead window.
- **Scratch baselines**: S1 = `0x00000000`, S1+4 = `0x00000000`, S2 = `0x00000000`,
  S2+4 = `0x00104427` (both aliases equal). Recorded verbatim in
  `build/register-dumps/exp/20261004-163526/scratch-baselines.txt`.

## Seven-offset corroboration against the frozen normal-op dump

The baselines file (`scratch-baselines.txt`) re-reads all seven live values against
`opensource/build/register-dumps/barmap_ep0_bar0.bin` (md5 `40a1da524539c3392d3c04694ffafb4d`) with
`struct.unpack_from('<I', image, offset)`:

| CA read | live | barmap | match |
| --- | --- | --- | --- |
| `0x4017D560` | `0x00040295` | `0x00040295` | yes |
| `0x4017D4E4` | `0x000462F9` | `0x000462F9` | yes |
| `0x406B8000` | `0xE59FF018` | `0xE59FF018` | yes |
| `0x401035A0` | `0x00000000` | `0x00000000` | yes |
| `0x401035A4` | `0x00000000` | `0x00000000` | yes |
| `0x40103EB4` | `0x00000000` | `0x00000000` | yes |
| `0x40103EB8` | `0x00104427` | `0x00104427` | yes |

All seven match: the takeover image carries the same static content at these offsets as the normal-op
dump.

## The intrsamp observation (the masked-status latch, unchanged from phase 46)

The `intrsamp=1` knob produced (`dmesg.txt:770-775`), quoted as-is:

```
[intrsamp] pre: ack(out5)=00000000 raw(0x2e4)=00000000 masked(0x2ec)=00000010
[intrsamp] doorbell out[2] <= 0x00000001 (bit 0) readback=0x00000000
[intrsamp] CHANGE val 1 iter 0: ack 00000000 -> 00000000, raw 00000000 -> 00000001, masked 00000010 -> 00000011
[intrsamp] doorbell out[2] <= 0x00000008 (bit 3) readback=0x00000000
[intrsamp] CHANGE val 8 iter 0: ack 00000000 -> 00000000, raw 00000001 -> 00000009, masked 00000011 -> 00000019
[intrsamp] done: ack=00000000 raw(0x2e4)=00000009 masked(0x2ec)=0x00000019
```

`build/register-dumps/exp/20261004-163526/dmesg.txt:770-775` (mirrored as `module-log.txt`). The doorbell
raises raw and masked: bit 0 sets on the first ring (`0x0 -> 0x1`), bit 3 sets on the second
(`0x1 -> 0x9`). The pre-existing masked bit (`0x10`) was already latched from the boot dialogue. This
reproduces phase 46's mechanism at the register level; the ack (`out5`) stays 0 throughout, consistent
with it being the write-to-clear `HOST_INTR_CLR` the task-4 strike describes (see `THE-LAST-LINK.md`
section 3's correction block), so it decides nothing.

The `[sig]` line is present (`dmesg.txt:717`: `[sig] 9/9 signature registers changed -> THE CHIP LEFT ROM
STATE`), the firmware readback matched (`dmesg.txt:695`: `firmware readback diffs=0 match=YES`), and the
done marker landed (`dmesg.txt:1122`: `init done wiphy=omo-drv1 ... regs=decoded`).

## Knob composition (why one boot, not two)

`KNOBSET.txt` records params `hw=1 wr=1 fw=1 release=1 program=1 fwctx=1 intrsamp=1` - the frozen
non-default set (`tools/params/definitive-run.params`, sha256
`541060a873d58913b9d63457dc8d5b42709d5043549daebcb4f8f208f5c5642c`) plus the task-9 additions. `msgsvc`
is NOT set: task 3's verdict is (a) (`build/register-dumps/diffs/20261004-160609/glue-2ec-contradiction.md`).
The composition path has no mutual exclusion (`wifidrv1.c:2088` program -> `2105` wr/ete -> `2113` fw ->
`2143` release -> `2189` fwctx -> `2197` intrsamp), so a single boot is correct.

## The `[fwctx]` dump

`fwctx=1` printed the runtime context object (`dmesg.txt:718-753`): the global at `@firmware 0x172130`
(BAR0 `0x82a130`) = `0x0010c0f4`, the ctx dump at offsets +144..+176 carrying the six mailbox CAs
(`40039014 40039010 40101434 400392f0 400392d4 40101418`), and the two handler tables at `@firmware
0x118d68` and `@firmware 0x17d430` (the latter's first three entries `000c2081 000c20a3 000c20f9`). Read
live, not asserted.

## What this settles, and what it does not

1. `register(0x4c)` RAN in the takeover boot: the `fn_array[0x4c]` slot holds the fn-pool pointer, with a
   populated control neighbour and a decoded window in the same read set. GATE G2's nonzero branch holds.
2. The interrupt IC-side latch behaves exactly as phase 46 measured: doorbell -> raw and masked bits set,
   ack unmoved (W1C). The *delivery* hop is untouched by this run; task 11's scratch boot tests it.

## Health and cleanup

`health.txt`: `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. Two
cycles failed before this one: cycle 1 used the wrong done marker; cycle 2's dir
`build/register-dumps/exp/20261004-163203/` is kept - it hit the capture-hook remote/local path bug
(later fixed and reverified, task 6's `checkbox-reset` in the ledger).
