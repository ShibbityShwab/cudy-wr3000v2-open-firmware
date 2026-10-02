# wifidrv1 end-to-end: both register blocks decode (phase 23f, 2026-10-02)

The completion of the phase-23 window work. Two device-measured corrections and one open question,
recorded exactly as they stand.

## The result

Run `tools/exp.sh wifidrv1.ko hw=1 program=1 verbose=1` (detached, watchdog armed first):
`WIFIDRV1 RESULT: PASS`, evidence `build/register-dumps/exp/20261002-140206/`.

In one boot, with the six inbound viewports programmed first, **both register blocks decoded**:

```
omo-drv1: BAR0 base=0x40000000 (config) cfg[0x004]=0x0006 MEM|MASTER=set
omo-drv1: mapped message BAR0+0x3f0000 and ETE BAR0+0x3f2000 (region-3 viewport at 0x40000000)
omo-drv1: SR ch0 CA=0x4003a400 base=0x00000000 depth-1=0 wptr=0x00000000 rptr=0x00000000 ctrl=0x00000000
omo-drv1: SR ch1 CA=0x4003a514 ... SR ch2 CA=0x4003a628 ...
omo-drv1: DR ch0 CA=0x4003a590 ... DR ch1 0x4003a5fc ... DR ch2 0x4003a668 ... DR ch3 0x4003a6d4 ...
omo-drv1: MSG0 out[0]      BAR0+0x00010 = 0x40000004
omo-drv1: MSG1 out[1]      BAR0+0x00014 = 0x00000000
omo-drv1: MSG2 doorbell    BAR0+0x002d4 = 0x00000000
omo-drv1: CHN_RES          BAR0+0x002e8 = 0x00000000
omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded
```

**No `0xffffffff` anywhere** - which is the point, because that value is the mis-address signature
(see `register-windows.md`). The ETE ring block reproduces the phase-20 channel geometry exactly (3 SR
at CA `0x4003a400` stride `0x114`, 4 DR at `0x4003a590` stride `0x6c`), and the ring registers read
zero because nothing has programmed them - correct for a takeover boot where the vendor's
`pcie_ete_init` never ran.

## The three-run progression, which is how the mapping was settled

| run | message window | ETE window | reading |
| --- | --- | --- | --- |
| `20261002-120525` | `0x39000` | (not mapped separately) | everything zero - both addresses effectively wrong, nothing decoded |
| `20261002-135752` | `0x39000` | `0x3f2000` + viewports | ETE decoded (`0x00000000`), message read `0xffffffff` - the swap that exposed the second bug |
| `20261002-140206` | `0x3f0000` | `0x3f2000` + viewports | **both decode** |

## The open question, stated plainly

`out[0]` read `0x40000004`. That exact word is a familiar one in this project - it is the value of the
**BAR0 config-space register** (`BAR0 0x40000004` appears in `docs/phase11/hwprobe.md`,
`docs/phase16/endpoint-init.md` and `docs/phase22/rc-routing.md` as the endpoint's BAR register).
Phase-20's own takeover measurements saw `out[0]` as `0x00000000` -> `0x00000008` (the bit-3 H2D mask)
under its own ring programming.

So `0x40000004` is **either** (a) a genuine live mailbox value - bit 2 set, plus a high bit pattern -
**or** (b) an aliasing artefact of reading at `BAR0+0x3f0010` in a state where the vendor's region
driver is not the one that bound the window. I am **not** calling it either way from this evidence
alone: the honest position is that the decode is now trustworthy enough to *get* a value, and the next
step is to disambiguate it.

How to disambiguate (cheap, read-only, one boot): read `out[0]` at three addresses in the same run -
`BAR0+0x3f1010` (the vendor table's offset), `BAR0+0x39010` (the old wrong one) and `BAR0+0x3b8000+0x10`
(the region base plus the raw CA offset) - and compare against the endpoint's config-space BAR0 word
read via `pci_read_config_dword(dev, PCI_BASE_ADDRESS_0)`, which the driver already prints. If the three
agree with each other and differ from the config word, it is a real mailbox value; if any equals the
config word bit-for-bit, it is aliasing.

## Device state

Recovered and healthy after the run: `W=2`, `I=6`, calibration `[SUCC]` on both bands,
`OMO_OFF=0`, no staged module, no watchdog, **0 real kernel faults**, pstore unchanged. CA
`0x400392f0` never written; the RC misc window `0x10161000` never read.
