# The first write path: executed, zero failures, viewports match the vendor (phase 23l, 2026-10-02)

Evidence `build/register-dumps/exp/20261002-150844/`, `WIFIDRV1 RESULT: PASS`,
`params=[hw=1 program=1 wr=1 verbose=1]`. This is the run that follows the oops
(`write-path-oops.md`); with the message window correctly mapped, everything completed.

## 1. The viewport differential: MATCH, viewport for viewport

The driver read back all six inbound viewports it programmed, and every field equals the live vendor
reference (`build/register-dumps/bothep/001_live_vendor_bars2.txt`):

| viewport | ctrl2 | base | limit | target | vs vendor |
| --- | --- | --- | --- | --- | --- |
| v0 (ROM_WRAM) | `0x80000000` | `0x40000000` | `0x401bffff` | `0x00000000` | **match** |
| v1 (TCM) | `0x80000000` | `0x401c0000` | `0x401d7fff` | `0x00400000` | **match** |
| v2 (PKTRAM) | `0x80000000` | `0x401d8000` | `0x403b7fff` | `0x01000000` | **match** |
| v3 (IO) | `0x80000000` | `0x403b8000` | `0x404d7fff` | `0x40000000` | **match** |
| v4 (ACP) | `0x80000000` | `0x404d8000` | `0x406b7fff` | `0x02000000` | **match** |
| v5 (ACP-fw) | `0x80000000` | `0x406b8000` | `0x408cffff` | `0x01200000` | **match** |

**This settles the aliasing question.** Our viewport programming is byte-identical to the vendor's, so
the `out[0]` config-space alias found in the disambiguation run is **not** caused by a wrong viewport
value. The remaining difference must lie elsewhere (endpoint/RC selection at read time, or the read
path itself) - and since we now know the viewports are right, that search has a much smaller space.

## 2. The first write path: 3 SR + 4 DR rings + both binding writes, 0 failures

```
---- first write path: ETE ring programming ----
intr pre=0x3f3f1f1f mask=0xffe0f8f8
SR ch0 base  [0x0410] <= 0x83790000 readback=0x83790000 match=YES
SR ch0 depth-1 [0x0414] <= 0x0000001f match=YES      (...ch1, ch2 likewise)
DR ch3 base  [0x05c0] <= 0x8222a000 match=YES         (...ch4, ch5, ch6 likewise)
glue chn_res pre=0x00000000 mask=0xfffffc20
glue chn_res 0x400392e8 [0x02e8] <= 0x00000000 match=YES
---- write path done: writes that failed readback = 0 ----
```

Every register the vendor's `pcie_ete_sr_reg_init` / `pcie_ete_dr_reg_init` sequence touches was
written **and read back matching**, and the driver counted its own failures: **0**. The
`pcie_ete_chn_res` glue RMW achieved the mandated `& 0xfffffc20` on CA `0x400392e8`.

The port can now **own the endpoint's ring state**, which was the stated goal of the write path - the
prerequisite for any data-path attempt, and explicitly not a data path itself (no descriptor was
submitted, no doorbell rung, by design).

## 3. Device state

`health.txt`: `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`.
Confirmed independently after the run: `WIPHY=2`, `IFACE=6`, calibration `[SUCC]` on both bands,
`OMO_OFF=0`, no staged module, no watchdog, **0 real kernel faults**, and **pstore unchanged** (blk-0/1/2
mtimes still `00:46/00:47/00:47` from the earlier oops - this run produced no crash record at all).

Slot A remains stock. CA `0x400392f0` was never written; the RC misc window `0x10161000` was never read;
no vendor module was unloaded.

## 4. Where this leaves the port

| layer | state |
| --- | --- |
| board description | reconstructed DTS |
| kernel struct ABI | solved + re-confirmed live |
| wiphy + netdev | proven on hardware |
| endpoint claim | works in takeover; `EBUSY` under the vendor stack |
| inbound viewports | **programmed and byte-identical to the vendor (this run)** |
| ETE ring block | decodes; ring geometry matches the phase-20 channel table |
| **ring ownership** | **written and verified with 0 readback failures (this run)** |
| message block read | still aliases config space - narrowed, not solved |
| data path | not started; phase 22's device-side accept gate remains the frontier |

Two bugs were found by measurement getting here: the wrong message window (`0x39000` vs `0x3f0000`)
and the undersized ioremap (`0x1000` vs the `0x1508` access). Both produced unambiguous signals -
`0xffffffff` for the former, a paging oops for the latter - and both are now covered by explicit
checks (the offset-fits-its-window sweep).
