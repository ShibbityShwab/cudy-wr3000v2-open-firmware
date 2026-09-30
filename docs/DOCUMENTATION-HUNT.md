# Documentation hunt: what exists for these chips (2026-10-01)

The rule this session follows: research before declaring something unknown. So here is what a
deliberate hunt turned up for the Hi5671Y SoC and the Hi5622V100 radio, with the honest implications.

## What is publicly available

| Source | Status |
| --- | --- |
| Hi5671 / Hi5671Y / Hi5671YV200 datasheet or product brief | **Not public.** HiSilicon ships these under NDA. What circulates is third-party synthesis (carrier product pages, teardown summaries) with no register-level content. |
| Hi5622 / Hi5622V100 datasheet | **Not public**, same situation. The community material is a class description (AX3000, 2x2+2x2, pairs with Hi5671-class SoCs). |
| PCI IDs | `59e7:0005` is not in the public PCI database; the subsystem id decodes as "Huawei Technologies" (`19e5`). No vendor datasheet follows from the id. |
| FCC filings | Cudy's grantee id is `2APRG`. `fccid.io` and `fcc.report` are bot-gated to plain fetchers; a browser session (or the user's own browser) can pull the internal-photos PDFs, which show chip markings and the RF front-end parts. Not yet retrieved. |
| HiSilicon's own public trees | `hisilicon/linux-hisi` exists but targets camera/STB SoCs. No router SoC, no `luofu`, no `hi5671`. |
| Community open-SDK work | `OpenIPC/openhisilicon` ("Opensource Hisilicon SoCs SDK") is the closest precedent: a reverse-engineered open SDK that replaces proprietary modules on **camera** SoCs (Goke/HiSilicon), built over years by a community. It does not cover router SoCs, and its interfaces are far better documented than this radio's. |
| Search for `hi5671y` / `luofu` / router SoC ports | Nothing public. The top hit for "hisilicon luofu" is now this project's own repository. |
| Sibling chips | The Hi1152 (HiSilicon Wi-Fi 6, same class) has the same verdict in community threads: no open driver, no mainline support. |

## What that means, stated plainly

1. **The documents that would make this "fast and mistake-free" do not exist publicly.** The two missing
   artifacts are the SoC register map and the radio's firmware interface. Everything else (the PCIe
   identity, the BAR sizes, the driver's structure, the calibration API, the firmware image) we already
   have from the device itself.
2. **The precedent shows the shape of the work, and its cost.** OpenIPC got there for camera SoCs with a
   community, years of effort and far simpler interfaces. A router SoC plus a closed Wi-Fi radio is
   strictly harder, because the radio's MAC/PHY behaviour lives in an on-chip firmware whose protocol
   must be inferred from traces.
3. **What is genuinely achievable, and what is being done now:** the register-map skeleton from the
   driver's disassembly, the firmware image's structure from disassembly, and the driver-to-firmware
   message flow from kprobe captures. Those three pieces are the necessary first third of any port and
   they are all in progress with artifacts under `docs/phase3` and `docs/phase4`.
4. **What no amount of skill removes:** writing replacement radio firmware requires knowing the core's
   ISA and its calibration algorithms. Neither the datasheet nor the SDK do, and the blob is the only
   copy in existence. That is why the GPL request matters: the vendor holds the source, and the module
   declares `license=GPL`.

## The one action that would change the calculus

Retrieving the FCC internal photos (a browser session) would confirm the RF front-end part numbers.
That does not unlock a driver, but it closes the last open hardware questions. Everything else waits on
either the vendor's source drop or the reverse-engineering milestones already running.
