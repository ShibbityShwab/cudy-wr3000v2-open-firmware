# RESOLVED: there was never any aliasing - the log labels were wrong (phase 23n, 2026-10-02)

This supersedes the "config-space aliasing" conclusion in `window-disambiguation.md` **and** the
"one page too high" reading of the same data. Both were wrong, and the cause of both was a defect in
**this module's own logging**, not in the hardware or the offset table.

## What was actually happening

The disambiguation function read **relative** offsets from the mapped windows:

```c
pr_info("omo-drv1:   out[0]   BAR0+0x3f1010      = 0x%08x\n",
        omo_rd(omo_msg, OMO_MSG0));        /* omo_msg + 0x010, i.e. BAR0+0x3f0010 */
```

The label said `0x3f1010` (absolute) while the read was at `0x3f0010`. That single mismatch made the
values look like they came from the wrong address - and `0x000559e7` / `0x40000004` happened to be
the PCI vendor:device id and the BAR0 config word, which made "config-space aliasing" a perfectly
plausible story. It was a story, not a measurement.

## The control that settles it (vendor boot, read-only `devmem`)

```
0x403f0000 = 0x000559E7     <- matches my "msg+000" read exactly
0x403f0010 = 0x40000004     <- matches my "out[0]"  read exactly
0x403f0014 = 0x00000000     <- matches my "out[1]"  read
0x403f02e8 = 0x00000000     <- chn_res, matches
0x403f2000 = 0x0000010A     <- ETE block, matches
```

**Every value the module read equals the vendor boot's value at the address the module actually
read.** The message block is at `BAR0+0x3f0000`, `out[0]` is at `+0x010` = `0x3f0010`, and the
module had that right from the `phase23d` fix onward.

So: **no aliasing, no off-by-a-page, no hardware mystery.** `0x000559E7` is a genuine device word at
device CA `0x40039000`; it merely *looks* like a config-space value, which is what sent this
investigation down a false path for two phases.

## The fix

The labels now print the absolute offset they are derived from (`OMO_MSG_WIN + offset`), and the
misleading probe that read a stale `0x39010` offset is gone. Commit `43f62b1`.

## The lesson, which is the useful part

Two phases went into a phantom bug because the log lied about *where* it read. The diagnostic rule
recorded earlier ("a mis-addressed window reads `0xffffffff`") is still true, but this episode adds a
sharper one:

**A value that looks like it came from the wrong place may simply have been mislabelled.** Before
building a theory on "these bytes are config space", confirm the address in the log is the address the
code used - print the computed absolute address, never a hardcoded string beside a relative read.

Cost: two device boots and a wrong written conclusion. Benefit: the module now logs addresses it
actually computes, so the next reader cannot be misled the same way.

## Standing corrected, usefully

- `register-windows.md` - **stands.** The offset table was right throughout.
- `write-path-executed.md` - **stands.** The ring writes and their readbacks were real and correct.
- `window-disambiguation.md` - **its conclusion is retracted** by this file; the measurements in it are
  valid, the interpretation was not.
- The port's state is unchanged and better than it looked: the message block decodes correctly, which
  means the H2D dialogue phase 22 identified is one step closer than the aliasing theory implied.
