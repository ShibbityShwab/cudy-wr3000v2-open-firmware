# Open-source request: Cudy WR3000 v2.0 Wi-Fi driver + firmware SDK (phase 25q, 2026-10-03)

This is a **REQUEST artifact, not a result.** Nothing below claims the source has been obtained. It exists
so the human can send it without doing further research, and so the ledger can record the one remaining
input that is not mine to fetch.

## Status, stated plainly

The remaining device-side gate - why the firmware reads the port's well-formed message and does not act on
it - has not been moved by host-side work. Eleven host-side candidates have been tested and eliminated
across phases 24-25. The two inputs that could still settle it are (a) the vendor's source, and (b) a
device-side trace. **(a) requires you.** No amount of further work in this repo substitutes for it, and no
progress claim here should be read as making it unnecessary.

## The request route (verified today)

Cudy publishes GPL source through its Download Center:

1. **`https://www.cudy.com/pages/download-center`** - select the exact model and hardware version.
2. On the model's page there is a **GPL** section with a source archive, where one is published.
3. If **WR3000 v2.0** has no GPL entry (likely - see below), that absence is itself the thing to raise:
   the obligation does not depend on the vendor choosing to publish a convenient archive.

## Why the request is well-founded

- The **Wi-Fi driver is a Linux kernel module** (`hi5622v100_wifi.ko`, `hi5622v100_plat.ko`, built for
  kernel `5.10.201`, `vermagic=5.10.201 SMP mod_unload ARMv7`). A module that links against the Linux
  kernel is a derived work, so the **GPLv2** obligation attaches to its source, not merely to the kernel's.
- Both modules are present on the device and their hashes are recorded, so the request can name exactly
  what is wanted rather than describing it.

## Components to request

1. **`hi5622v100_wifi.ko` sources** - md5 `e21629d226ec7de9a860a8955952d311`.
2. **`hi5622v100_plat.ko` sources** - md5 `23660bc285393e678d5cade1c36c194b`.
3. **The build scripts / SDK configuration** that produce them, plus the **firmware build** for the on-chip
   image (`/lib/firmware/hi_wifi/FIRMWARE.bin`, md5 `0e530b976d5a20e87358671f1a577695`, 928,920 bytes) -
   the firmware is the other side of the protocol I cannot read directly.
4. Ideally the **HCC message/command table documentation** or the header defining the message ids - which
   is precisely the artefact phase 25 needed and could not derive from the binaries.

## Independent corroboration found while preparing this

A web search for the request route returned a vendor-adjacent source stating that **"only the WR3000 v1 is
supported by OpenWrt; the v2 is not supported (different flash/chipset), and Cudy has stated there is no
OpenWrt firmware for WR3000 V2.0"** - which independently agrees with this repo's own conclusion
(`CUSTOM-FIRMWARE-PLAN.md` section 2: no upstream target, no open-source Hi5622V100 driver). Two
independent sources now say the same thing, one of them the vendor.

Sources consulted: `https://www.cudy.com/pages/download-center` (route) and the OpenWrt/Cudy support pages
surfaced alongside it. **The URLs were found by search and not fetched here** - so treat the route as
reported, and confirm the page before sending.

## What this file is NOT

It is not evidence that the source is obtainable, nor that it would close the gate. It is the request, and
until a reply arrives the goal that depends on it stays open.

