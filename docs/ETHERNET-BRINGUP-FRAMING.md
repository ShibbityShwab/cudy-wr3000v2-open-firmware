# The Ethernet bring-up, framed — and what actually blocks the goal

**Written 2026-10-14, immediately after the NIC started working.** This is the conceptual pass: what
each bug *was a species of*, why the same shape kept recurring, and what the remaining wall is made of.
Read this before touching `lab/luofu-gmac` or `lab/luofu-mdio` again.

---

## 1. The headline, measured

The NIC works. Not "compiles", not "probes" — **the MAC is configured from a live PHY link**:

```
C88 = 0x00000000   phylink_of_phy_connect() returned 0 — the PHY is found and attached
C84 = 0x0000CDEF   PHY 4 BMSR: LINK SET (bit 2), AUTONEG COMPLETE (bit 5)
C98 = 0xC0DE7E00   of_mdiobus_register() returned 0
C1C = 0x02026102   low half 0x6102 = 0xC0DE6100 | 2 = SPEED_100
                   ^ that value is written by ONE function: luofu_gmac_mac_link_up()
```

`C1C`'s low half is the surviving channel — the FMC's 5-second tick carries `luofu_net_diag` in its
payload, and that value outlives the fallback because it lives in a register block, not RAM. **It is the
single most trustworthy instrument on this bench.** Everything else has lied at least once (see §4).

---

## 2. The four bugs, as species

They were not four unrelated mistakes. They were **two species, and the species are what matter.**

### Species A — "present in the device tree, bound by nobody"

A block is described in `luofu-r116.dts`, no driver claims it, and therefore **its clock is never
enabled and its reset is never released.** The block then sits inert and the failure surfaces far away
as something that looks like a protocol error.

| instance | the far-away symptom | the actual cause |
|---|---|---|
| MDIO block | `-EIO` out of `of_mdiobus_register` | `devm_reset_control_array_get()` fetched, **never deasserted** |
| Internal LSW switch | gmii PHYs never negotiate | both switch nodes `disabled`, nothing bound them |
| Peripheral pinctrl | pins never configured | `status = "disabled"` **and** a compatible-string mismatch |

**The tell for this species:** the error code does not match the layer reporting it. Clause-22 PHY paths
converge on `-ENODEV`/`-EINVAL`; getting `-EIO` out of `of_mdiobus_register` meant **a transfer fault
wearing a registration error's name**, and the fix was two lines up in a reset call.

**The detection recipe:** for anything in the DT that "should be working", ask *which driver binds this
node*, and if the answer is "none", check that its `clocks` and `resets` are actually consumed.

### Species B — "registers successfully, configures nothing"

The driver *does* bind and *does* report success, but the part that actually makes the hardware do
something is a stub or is never reached.

| instance | what success looked like | what was missing |
|---|---|---|
| MAC port config | netdev registered, PHY attached, crumb `0xC0DE6006` | `mac_config`/`mac_link_up` were **empty stubs** |
| the netdev itself | `register_netdev()` returned 0 | **nothing ever opened it**, so phylink never started |

**The tell for this species:** the crumb chain *completes* and the hardware still does nothing. A
"healthy-looking" boot is the failure mode. **`mac_link_up` never firing was the fingerprint**, and it
was invisible until a value that ONLY that function writes (`0xC0DE6100 | speed`) was placed in the
surviving cell. That marker is the reason this bug was ever found.

**The detection recipe:** for any bring-up stage that reports success, name **the register write that
proves the stage happened**, and put a value only that write can produce where you can read it after
the fallback.

### And one mapping bug that belonged to neither

**The MAC↔PHY map was shifted by one.** Our tree said `14300000→phy2, 14380000→phy3, 14400000→phy4,
14480000→phy5, 14500000→phy1`. The vendor's own dmesg and sysfs say:

```
14380000.gemac eth0 -> mdio0:02     14400000.gemac eth1 -> mdio0:03
14480000.gemac eth2 -> mdio0:04     14500000.gemac eth3 -> mdio0:05
14300000 appears NOWHERE
```

So **the port carrying the cable was bound to the wrong PHY**, and one PHY in our tree did not exist.
Nothing about this is inferable from the schematic — it was read off the running vendor.

---

## 3. The method that worked, stated once

**The vendor's running kernel is the specification.** It is the only object in the world that has this
silicon doing the right thing, and its registers can be read through `/dev/mem`.

- **To learn a register's value:** diff a device that is *working* against one that is *not*, on the
  vendor, live. That is how the MAC's `PORT_MODE` value was obtained (`0x010C0008` on a live 1 Gbps
  link vs `0x000C0408` on an unlinked one) — and it is **not** the value the mainline driver would have
  written for a different IP revision (`GMAC_SPEED_1000 = 0x05`).
- **To learn a mapping:** read the vendor's dmesg and `/sys/class/net/*/phydev` symlinks. Not the
  schematic, not the vendor device tree in isolation.
- **To learn a protocol's layout:** disassemble the vendor's own module. The MDIO register map
  (`CMD 0x00 / WSTATUS 0x60 / RSTATUS 0x6c / FRAME 0x70 / RDATA 0x74`) came out of `hi_kmdio.ko`'s own
  `hi_mdio_read`, not from a datasheet.
- **Do not poke the vendor's controllers from userspace.** Poking its MDIO returned all zeros, because
  its PHY state machine polls that same controller continuously. Read through **our own** driver
  instead, and accept that this means you only learn things at boot time.

**Corollary: a bring-up bug is often two layers from its symptom.** Every fix above was found by
walking *away* from the error and reading the code that produced the state.

---

## 4. The instrumentation problem — and it is the real remaining wall

**Our kernel's log dies with its RAM.** By the time ssh can reach the box, the fallback has happened
and the vendor has overwritten our memory: dumping `__log_buf`'s physical address after a fire returns
the **vendor's** data (measured: 1113 lines of its string tables). So the boot's last words are
unreachable.

**pstore on `mtd10` is the only thing that could survive the fallback, and it still does not register.**
The chain is traced (`pstore_blk_init` requires `!pstore_zone_info && best_effort && blkdev[0]`;
`pstore_blk_get_config()` copies `blkdev` into `info->device`; `mtdpstore_init` needs `info->device`
non-empty and `kmsg_size` non-zero, then `kstrtoint()`s it — a number means an mtd index, a non-number
sets `index = -1` so its notifier matches **by name**). Both `pstore_blk.blkdev=pstore` and
`best_effort=1` are set and verified in the binary. It still does not take.

**Everything that lied, and how, is worth listing because these cost a full day:**

| instrument | the lie |
|---|---|
| `strings zImage \| grep X` | zImage is COMPRESSED — always 0, for everything |
| `strings vmlinux \| grep X` (X in a C comment) | comments are not in the binary |
| `objdump` | **not installed** — produces no output, so `grep -c` counts zeros and looks like a clean negative |
| `diff repo-copy tree-copy` | always differs — CRLF vs the script's stripped tree copy |
| **the build log** | `make` keeps the PREVIOUS `.o` when a file fails to compile, and the pipe-masked script still printed `zImage OK`. A missing `#include` shipped the OLD driver twice |
| **the incremental build, once** | `wsl_build_incr.sh` links `vmlinux` **before** the copied object rebuilds, so a change misses the image. **It needs TWO passes** (measured: pass 1 `uImage 2658488 B`, pass 2 `2658680 B`) |
| the diagnostic cells | **they persist across warm reboots** — a stale `0xCDEF` read exactly like a fresh measurement for two fires |
| the fire script's ring read | read a **hardcoded** `0x80603000`, correct only while `LOG_BUF_SHIFT` was 16. Raising it moved the buffer |
| `luofu.fragment` | `CONFIG_LOG_BUF_SHIFT` appeared **twice** (18 then 16) — the last wins, so the ring silently stayed 64 KiB |

**The checks that actually work:**
- `arm-linux-gnueabihf-nm -u <driver>.o | grep <symbol>` — proof the call compiled in.
  Remember inlines: `phy_read()` is `static inline`, so the real symbol is `mdiobus_read`.
- `strings` the **uncompressed `vmlinux`** for a string you just added.
- md5 the artifact **before and after** and require it to change.
- the surviving `C18`/`C1C` pair, cross-checked against a `devmem` from the same fire.

---

## 5. The remaining wall, stated plainly

**Our kernel runs ~78 seconds and returns, and nothing answers on the wire.**

```
t=  0..21s   ssh OPEN, VENDOR     (before the reboot)
t= 28..105s  ssh CLOSED           <- OUR KERNEL, ~78 s — it booted and ran
t=112s+      ssh OPEN, VENDOR     <- the fallback took it back
```

**It is not the safety timer.** `LUOFU_SAFETY_SECS` is 900 and its handler is `emergency_restart()`
(`luofu-fmc.c:luofu_reboot_tick`). 78 s cannot come from it. `panic=5` reboots on a panic, and u-boot's
failover then hands the box to the vendor — so **a panic in the later boot is the leading explanation**,
and it is unobservable for the reason in §4.

**Kernel-side IP did not produce reachability**, and the reason is now known: this is an **initcall
ordering** problem, the same species as `ubi_init` vs the FMC's `late_initcall_sync` earlier in the
project.

```
ip_auto_config   late_initcall            (level 7)
gmac, mdio       module_platform_driver   (level 6)
```

The gmac **defers** until the MDIO has registered its PHYs, and a deferral is retried from
`deferred_probe_work` — which runs **after** every initcall level completes. So `ipconfig` at level 7
ran before the deferred retry created the netdev, and `net/ipv4/ipconfig.c` matches its interface
**by name** (`!strcmp(dev->name, user_dev_name)`) and gave up with "Device `eth2` not found". The fix
(moving the MDIO to `subsys_initcall`, level 4) is committed but **unverified** — the next fire after
it still returned, so either the ordering was not the whole story or the boot dies before it matters.

---

## 6. What to do next, in order

1. **Get one instrument that survives a boot.** Until then every step is inference. Prefer: instrument
   `mtdpstore_init` itself with a crumb (do not guess again), or `panic=-1` so the box *hangs* instead
   of rebooting — a hang keeps our kernel in place and, with `ip=` working, could still be reachable.
2. **Confirm the initcall fix.** Read `C1C` for `mac_link_up` (proves the network came up) and whether
   an address appeared — `ipconfig` prints to the console, which the ring *would* hold while our kernel
   is running, if the ring were readable.
3. **Only then** chase the Wi-Fi half. The Ethernet wall is what made the box unreachable; the Wi-Fi
   reversal is the longer programme and does not need to be unblocked by hand.

**The honest scale:** the NIC was the gating subsystem and it now works. What remains between here and
"latest OpenWrt running on the board, our kernel, our drivers" is (a) boot survival, (b) an addressable
interface, and (c) the Wi-Fi driver — of which (a) and (b) are days, and (c) is the multi-month
programme the ledger already calls the long pole.
