# The library closure is the real constraint on "a newer userland in place" (phase 26d, 2026-10-03)

Phases 26a-26c established that a 24.10 userland runs on the vendor kernel and that the rootfs cannot
simply be swapped. The remaining question was whether individual packages could be upgraded **in place**
to approach "a newer OpenWrt" without the wholesale swap.

The measurement: of the **433 packages installed on the device, 172 exist in 24.10**, and the ones that must
not be swapped blindly are the critical path -

```
busybox  dropbear  procd  ubus  ubusd  libubox  libubus  netifd  libc  opkg
libuci   uci  wpad-openssl  dnsmasq-full  firewall4  nftables-json
```

But the blocker is not the list, it is **what upgrading pulls in**: every one of those 172 has a dependency
closure ending in `libc` (`libc.so` is musl here, unversioned), and most C packages also pull
`libopenssl`, `libubox`, `libjson-c` and friends. **Upgrading a leaf package to 24.10 means upgrading the
shared libraries it links, and those same libraries are what the 22.03 binaries in the vendor rootfs link
against.** So:

- a curated "safe subset" upgrade is not actually safe in isolation - the subset drags its libraries;
- keeping the libraries and upgrading only the binaries means mixing 24.10 binaries with 22.03 libraries,
  which is not a tested configuration (26b used a private lib path precisely to avoid exactly that);
- therefore **coherently newer means the whole closure, which is a rootfs-level operation** - and
  phase 26c showed what a rootfs-level operation costs (the `hi_*` app layer, the Wi-Fi calibration flow,
  the `hi_hi_upgrade_*` flashing path).

## The three strategies, with their real costs

| strategy | what it delivers | what it costs |
| --- | --- | --- |
| **A. Curated in-place upgrade** | newer leaf tools alongside the vendor base | not coherent - either the libs stay 22.03 (untested mix) or they move (breaks 22.03 binaries). Small real gain, unclear safety |
| **B. Full modern rootfs on the vendor kernel** | a genuine OpenWrt 24.10 userland | loses the vendor app layer, the Wi-Fi calibration scripts and the device's own upgrade mechanism; **Wi-Fi is expected not to work** |
| **C. From-source build (vendor source)** | the only path to a complete, coherent modern firmware | needs the vendor to answer the GPL request |

## Why this is a decision and not a calculation

**B is what "deploy a latest version of OpenWrt" literally means, and it is the one that is expected to
leave the router without Wi-Fi.** It is recoverable (slot A stays stock, the bootflag selects the slot, and
the recovery path is documented), but it trades the machine's Wi-Fi for the version string. A is safe but
delivers little. C is the only complete answer and depends on a third party.

The project's own safety rule - *leave the router healthy (2 wiphys, 6 interfaces, calibration [SUCC] on
both bands)* - is in direct tension with B, which is why B needs an explicit decision rather than an
inference from "as current as the hardware allows".

