# Deployed-build re-verification, a dangling symlink, and a correction to my own earlier claim (phase 25p, 2026-10-03)

Live re-verification of the deployed build, run fresh in this session:

```
marker      = omo-minimal-0.3 stock-2.5.24-20260727-122111
slot        = 14 (B)   bootflag=b
ubiblock0_0 sha256 = 55f5c5b40ca6f46f19302dcc9605c963
local artifact     = 55f5c5b40ca6f46f19302dcc9605c963   <- identical
revision    = 2.5.24
/etc/init.d/omosshd   present   /etc/rc.d/S95omosshd  present
```

So the deployable claim holds on its three key facts: the marker is the recorded one, **the running rootfs
hashes to exactly the artifact this repo builds**, and the injected SSH service is live (dropbear is
running, and it is how these probes are taken).

## The finding: a dangling symlink, and a claim of mine that was wrong

```
/etc/init.d/omo-rtmsg  = ABSENT
/etc/rc.d/S99omo-rtmsg = ABSENT (by [ -e ], which FOLLOWS the link)
ls /etc/rc.d/          -> S99omo-rtmsg     (ls LISTS the dangling link)
/overlay/upper/etc/rc.d/S99omo-rtmsg  exists, dated Oct 1 14:31
/overlay/upper/etc/init.d/            contains omosshd only - no omo-rtmsg
```

**`S99omo-rtmsg` is a dangling symlink**: the enable link is in the overlay, but the init script it points
at is not. The service is therefore **not** installed, and procd has nothing to start at 99.

And that means a claim in `docs/phase25/deployed-build-verified.md` is **wrong**: it says "the injected
layer is live `omosshd` + `omo-rtmsg`". It is not. The check that produced it was
`ls -l /etc/init.d/omosshd /etc/rc.d/S95omosshd /etc/rc.d/S99omo-rtmsg`, and **`ls` lists a dangling
symlink as though it were present** - so the test could not distinguish "installed" from "a broken link".

The corrected statement: **`omosshd` is installed and running; `omo-rtmsg` is a dangling enable link and
is not a live service.**

## The falsifiers, stated so the claim is checkable

| claim | would be false if | detected by |
| --- | --- | --- |
| slot B runs our build | the marker changed, or the slot switched | `cat /etc/custom-firmware-version`; `cat /sys/class/ubi/ubi0/mtd_num` |
| the deployed image is this repo's | the volume hash diverged from the artifact | `sha256sum /dev/ubiblock0_0` vs `sha256sum build/custom/rootfs-custom-0.3.sqfs` |
| the injected service is live | the init script is missing | `[ -e /etc/rc.d/<link> ]` **not** `ls` |
| slot A is the stock fallback | mtd13 changed | compare against the recorded dump |

## The lesson, which is the same one again

The first check in this phase also looked wrong for a moment - it appeared to show the whole rc.d
directory without any `S*` entries, which was simply `head -12` truncating an alphabetical listing. Two
apparent defects in one probe, one of them mine and one of them a truncation artefact, and **both were
resolved by testing the thing directly rather than reading a listing**. `ls` shows what exists; `[ -e ]`
shows what *works*. For a service-enable link, only the second is the question.
