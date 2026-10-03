# The custom firmware is built AND deployed - verified live (phase 25f, 2026-10-03)

Amid the protocol work, the objective's *deployable* half deserves a current, live verification rather
than a citation of older plan text. It passes.

## Three independent confirmations, all taken from the running device

```
/etc/custom-firmware-version  ->  omo-minimal-0.3 stock-2.5.24-20260727-122111
slot=14 (B)  bootflag=b
/dev/ubiblock0_0 sha256 = 55f5c5b4...   (FLASH-PLAN records the same)
local artifact build/custom/rootfs-custom-0.3.sqfs sha256 = 55f5c5b40ca6f46f19302dcc
/etc/init.d/omosshd + /etc/rc.d/S95omosshd   -> our procd dropbear service, start 95
/etc/rc.d/S99omo-rtmsg                        -> our injected rtmsg service
```

1. **The identity marker** on the running rootfs is the exact string `FLASH-PLAN.md` records for build
   0.3.
2. **The UBI volume's hash** matches the recorded artifact hash - and the local artifact still hashes to
   it, so the deployed image is the one this repo builds.
3. **The injected layer is live**: `omosshd` is present as a procd service (the dropbear instance that
   answers on port 22 - it is how these probes are being taken), and the `omo-rtmsg` service is
   installed.

## What this is, precisely - and what it is not

**Is:** a flashable, custom firmware image for this board, built in this repo, deployed to slot B, booting
and serving, with our access/package layer injected and a stock slot A kept as the fallback plus local
dumps of every partition for rollback.

**Is not:** a mainline/upstream OpenWrt build. The base is the vendor's own 2.5.24 (kernel + rootfs) with
our layer on top, because **no upstream OpenWrt target exists for this SoC** - established independently
earlier (`CUSTOM-FIRMWARE-PLAN.md` section 2, re-checked against upstream: no `hsan`/`luofu` target, no
open-source Hi5622V100 driver). So "latest OpenWrt on this hardware" is bounded by the vendor kernel's
ABI, and the honest deliverable is exactly what is deployed: a custom image, on a supported-kernel base,
with our layer and a recovery path.

## Why record it now

The plan documents describe the deployment as history. This is the same claim **re-measured on the live
device in the current session**, which is the standard this notebook holds itself to everywhere else -
and it means the deployable half of the goal is not merely documented but currently true.
