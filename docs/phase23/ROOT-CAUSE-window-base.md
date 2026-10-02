# ROOT CAUSE FOUND: the message window base was one page low (phase 23x, 2026-10-02)

The investigation that consumed this whole session's second half was **my own address arithmetic**.
The fix is one constant.

## The signature that broke it open

The run that read phase 19's CPU-start signature (`build/register-dumps/exp/20261002-182923/`)
showed **9/9 registers changed** - the chip was starting perfectly all along:

```
[sig pre ] BSS=b5e0ef5f/1351d023/aada134b/a71b7f5e dcoldo=ffffffff pbank=ffffffff abank=ffffffff tcxo=0/1
[sig post] BSS=00000000/00000000/00000000/00000000 dcoldo=260d4184 pbank=00000312 abank=0000010c tcxo=1/2
[sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE
```

`dcoldo_vset = 0x260d4184`, `pbank_code = 0x00000312` - byte-for-byte what phase 19 measured. So the
release works, the firmware runs, and it was **not** silent. I was watching the wrong addresses.

## The bug

The message CAs are `0x40039010`..`0x400392f0`.  Through region 3
(host `0x403b8000` -> dev CA `0x40000000`), the BAR0 offset is `0x3b8000 + (CA - 0x40000000)`:

| register | CA | correct BAR0 | the driver used | off by |
| --- | --- | --- | --- | --- |
| `out[0]` | `0x40039010` | `0x3f1010` | `0x3f0010` | `0x1000` |
| `out[1]` | `0x40039014` | `0x3f1014` | `0x3f0014` | `0x1000` |
| `out[2]` | `0x400392d4` | `0x3f12d4` | `0x3f02d4` | `0x1000` |
| chn_res | `0x400392e8` | `0x3f12e8` | `0x3f02e8` | `0x1000` |
| glue status | `+0x2ec` | `0x3f12ec` | `0x3f02ec` | `0x1000` |
| `out[5]` | `0x400392f0` | `0x3f12f0` | `0x3f02f0` | `0x1000` |

The window was mapped at `0x3f0000` instead of `0x3f1000`. **Every mailbox read, the glue-status poll
and the channel-resource write went to the page below the register they claimed to address.**

The interrupt register masked the error: it was coded as offset `0x1508` from the wrong base, which
lands on `0x3f1508` - the *right* address by accident. That is why the ETE writes and the release all
verified correctly while the mailbox looked dead.

## Independent confirmation from the vendor boot

`0x403f12e8` reads **`0x00000020`** - matching phase 15's captured `chn_res = 0x20` - while
`0x403f02e8` (what the driver read) reads `0`. Two independent sources agree on the correct offset.

## What it explains

Every negative this session, in order:

- "the mailbox never transitions after release" - out[1] was polled at `0x3f0014`; the firmware writes `0x3f1014`;
- "the glue status never asserts" - the status was polled at `0x3f02ec` instead of `0x3f12ec`;
- "the firmware produced nothing" after the firmware was loaded and verified - same cause;
- and the reason no amount of host-side work changed anything: the host was talking to empty pages.

## The lesson

The `0xffffffff` diagnostic rule from earlier in this session caught *unprogrammed* windows. This bug
produced **plausible values** instead - `0x40000004` at the wrong page was close enough to look like a
resting mailbox state, and it was accepted as one. The check that would have caught it immediately is
the one now applied everywhere: **derive each register's address from the documented CA and the
translation rule, and assert it against the vendor's own table** - never carry an offset that "looks
right" because a nearby register happened to verify.

Committed as `a3d8263`; the corrected build is CI run `37048119702`.
