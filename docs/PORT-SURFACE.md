# Port surface: what the vendor Wi-Fi stack costs to move (measured 2026-10-02)

Measured on the shipped binary `hi5622v100_wifi.ko` (3,564,728 bytes, `vermagic=5.10.201`,
`license=GPL`, `depends=hi5622v100_plat`). This file quantifies the "port it to a modern kernel"
option so the decision in `DEPLOYMENT-DECISION.md` rests on numbers, not adjectives.

## 1. It is not a mac80211 driver

| measure | value |
| --- | --- |
| cfg80211/wiphy symbols referenced | **15** distinct (`wiphy_new_nm`, `wiphy_register`, `cfg80211_rx_mgmt_khz`, `cfg80211_inform_bss_frame_data`, `cfg80211_connect_done`, `wiphy_apply_custom_regulatory`, ...) |
| mac80211 symbols referenced | **0** — no `ieee80211_` API surface at all |
| its own stack | registers netdevs directly (`alloc_netdev_mqs`) and holds an `ieee80211_ptr` purely for cfg80211 bookkeeping (error strings: `pst_net_dev->ieee80211_ptr is null ptr!`) |
| its own symbol surface | **4,330** strings in the `wal_`/`oal_`/`hcc_`/`bal_`/`hmac_`/`dmac_` namespaces |
| kernel symbols imported | ~5,859 candidate strings, 13 of them in the most version-sensitive families (`dev_*`, `sk_buff*`, `napi_*`, `pci_*`, `dma_*`, `netif_*`) scanned |

So the Wi-Fi MAC layer is **vendor-owned C inside the module**, not an upstream mac80211 driver with
vendor firmware underneath. `docs/DRIVER-BLACKBOX.md` reached the same conclusion from symbols; this
measurement fixes the size of the surface: a port replaces the kernel glue and keeps the vendor MAC.

## 2. The namespace is the hard part

The module imports **211 symbols through `import_ns=HW_RTOS_NS`** — a vendor symbol namespace. That
namespace is *not* an upstream mechanism being used; it is the vendor's own export mechanism, and the
symbols inside it do not exist in a mainline kernel or in `mac80211`/`cfg80211` as shipped:

- `cfg80211.ko`, `mac80211.ko` and `hi5622v100_plat.ko` import **no** namespace at all;
- only `hi5622v100_wifi.ko` does, 211 times.

A modern-kernel port therefore needs those 211 symbols provided by something — either the vendor's
own kernel patches (their SDK) or a re-implementation. **This is the single strongest argument for
Path A (vendor GPL sources) over Path B (clean-room)**: without the vendor tree, the port is blocked
at link time, before any driver work begins.

## 3. Consequences for each path

| path | what this measurement says |
| --- | --- |
| **A - vendor GPL sources** | The 211 namespaced symbols and the kernel patch that defines the namespace come with the SDK. Porting then means rebasing that patch series onto a newer kernel and fixing the 13 version-sensitive API families — a real but bounded job. This is why the GPL request (sent 2026-10-01, and the module's own `license=GPL`) is the highest-leverage action available. |
| **B - clean-room** | Reproducing 211 host-interface symbols plus a 4,330-symbol MAC stack with no documentation, then re-deriving the firmware's message service (which `docs/phase22/h2d-accept.md` shows we cannot yet arm even with full register access), is the years-long programme `PORT-PLAN.md` describes. |
| **C - current** | Nothing above matters for the deployed system: the vendor stack runs unchanged on the vendor kernel, in slot B, verified byte-identical to the build of record. |

## 4. What this changes in the plan

1. The reversing effort should **stop trying to re-implement the host stack** and instead keep
   mapping the *device* side (firmware behaviour at the mailbox/DMA boundary), because that map is
   what any future driver - vendor-ported or open - must satisfy.
2. Path A gains a concrete first deliverable beyond "build the SDK": **extract the `HW_RTOS_NS`
   provider** from the vendor tree and confirm it carries the 211 symbols. That is a checkable step.
3. Nothing here justifies touching slot B: the deployed image is the working system, and the port
   work happens on the host, not on the router.
