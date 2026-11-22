# Upstream submission package - luofu 6.18 kernel series

**Status: DOCUMENTATION-ONLY PACKAGE / RFC.  Nothing has been submitted
anywhere.**  This directory is a dry-run hand-off bundle for the owner to review
and, if they choose, send manually; it contains no accounts, no email transport
and no list subscription.  Package form: **v3 submission-form revision**,
regenerated 2026-11-22 from the current tree after the hardware PCIe GPIO fix
(patch 0013) and the board-DTS 2026-11-22 additions (see git log for the exact
commit).

## Current state (2026-11-22)

| item | value |
| --- | --- |
| kernel review form | 26 patches in `../series/` (RFC / do-not-merge split; see `../00-cover-letter.md`) |
| DTS-series entry | **DTS-0001** `../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`, sha256 `33956380db9ca5c7b91404596d6078d2f35e4a349978e41e4a1c5d5b9b5155fa` (712 lines, 699-line payload = the single-source DTS after the release-line W23 filter; `tools/dts-upstream-filter.py`) |
| single-source board DTS | **711 lines** sha256 `f24451ca5dd6b79724af05df8c4f978fc670bfe230b8931adaedd941cfb543c7` (2026-11-21 history: 691 lines 7ae18f2 -> 700 lines 5b401c1 -> 681 lines 6c2cc2f -> 681 lines 71b2167, sha 381b2e8c...; the 2026-11-22 additions are the enabled PCIe nodes, the `wifi-pcie-hog` and the identity `dma-ranges`) |
| compiled board DTB | **12,865 B** sha256 `d8b41ac37ec68a10ccaf2e68db0675dd6f28a83c1bb603603bbd97cc68af3ce3`; unaligned totalsize, `pad-dtb.py` pads to **12,872 B**.  DTS-0001 compiled from the filtered copy: **12,697 B** sha256 `288a5c3f3841572ecede2dc1aa223c06772c8bb2459cde31a95203cc1761763f`, pad **12,704 B** (2026-11-21 history: 12,678 B 62574b11... -> 12,680 B 8d79794e...) |
| current DTS-purpose hardware image | **W27b** (2026-11-22, image sha256 `4af1d1fa...`): built from the current board DTS (`wifi-pcie-hog`, enabled PCIe nodes, `dma-ranges`) with patch 0013 containing **no board-GPIO code**; booted with both RC links trained (LTSSM 0x11), both endpoints enumerated and GPIO0 DDR/DATA = 0x1330000f - the devicetree hog alone drives pins 20/21/28.  Boot-tested DTS-purpose baseline before the 2026-11-22 additions: **6e9c54d0** (2026-11-21) |
| release-line validator | PASS (2026-11-22 final): 55 include / 11 exclude, 55/55 patch(1) RC=0, 3 offset hunks, **0 fuzz**, strict git apply 55/55; DTS contract and DTS-0001-vs-filtered-target identity OK; manifest sha256 `98931166...`; logs `../validation/release-line/` |
| review form checks | 26/26 patch(1) and `git am`, identical trees, 3 offset hunks, 0 fuzz; checkpatch **0 errors / 73 warnings** (patch 0013 alone: 0 errors / 17 warnings - block-comment style + the known binding-split warning); PCIe `dt_binding_check` RC=0 (dtschema 2026.9); `gen-series.py --check` clean |
| current devicetree final | 2026-11-21 content: submodule commit **71b2167** (conservative cleanup 7ae18f2 + boot-tested TWD level-high + MAC/switch/compatible deltas + boot-tested flashinfo compatible-first; the superseded active cleanup **538377c** is regressed and NOT current).  2026-11-22 additions: **32d44e6** (wifi-pcie-hog + PCIe-stub cleanup), **5a6a500** (dma-ranges), **4e959f2** (patch 0013 hardware notes) |
| patch 0013 | **hardware-verified 2026-11-22 without board-GPIO driver code** - W25d/W26a trained the link and enumerated both endpoints; W27b (image sha256 `4af1d1fa...`) proves the DTS `wifi-pcie-hog` alone drives GPIO0 20/21/28, so the hardcoded workaround was removed (commit `ad711d1`) and the submission notes restored; still RFC: no MSI/INTx, single-function CFG0 topology, ACP gate is a bring-up workaround, binding split owed |
| remaining upstream blockers | **ZERO** for the 2026-11-21 documented blocker list - all active-DTS/code blockers closed; the drafted flashinfo binding remains maintainer-facing documentation only.  The 2026-11-22 DTS additions are hardware-derived pending one unit regression boot (not a new external blocker) |

## What is in this package

- `README.md` - this checklist, reproduction commands, Tested-by policy summary and blockers.
- `cover-email-draft.md` - one-page v3 RFC cover-email draft; **not sent**.
- `TESTED-BY-POLICY.md` - explicit policy: no upstream Tested-by/Reviewed-by is claimed.
- `MANIFEST.sha256` - sha256 of every artifact below (paths relative to this directory);
  verified with `sha256sum -c MANIFEST.sha256` from inside `submission/`.
  The manifest's own sha256 is recorded in the submission commit and the hand-off report.

## Artifact checklist (sha256 generated from the actual files)

| artifact | sha256 | role |
| --- | --- | --- |
| `../../../target-skeleton/dts/luofu-r116.dts` | `f24451ca5dd6b79724af05df8c4f978fc670bfe230b8931adaedd941cfb543c7` | single-source board DTS (outside the package tree) |
| `../00-cover-letter.md` | `c8d6cc83fe7fd6241f8ac45e7bc196942c9d04b59971ce6a0cb7558aa3efddbb` | submission document |
| `../01-commit-messages.md` | `898f3b6ad426ba7c154b01ea4e37480e4311d8318bebd994b9e1f972fd369122` | submission document |
| `../02-maintainers-proposal.md` | `30ae8a95b2786b3d60153f26fc4b8bf7b0c4b38b78e06e82e60b4a7f26d05c31` | submission document |
| `../03-rationale-tree-placement.md` | `7fdf3c34343a6004d69fc8cd55a055db02b0e471f363c91270e54889179350fa` | submission document |
| `../04-validation.md` | `2ce48b4ba668ac838333d7ad339f9b7be9a7036f4fb765219e815372ba972f8b` | submission document |
| `../05-release-line.md` | `d625d662ec1c047691f0c7fc8760e5323df4afa59f4976f032b312f99758608f` | submission document |
| `../06-dts-series-dependency.md` | `ab4303c403b2e8d07c69d271e80b15061b59f95fe4ca38704c36a89832d4a064` | submission document |
| `../07-dts-upstream-bindings.md` | `a06fc7b78b6fa455e9d16634ae87cca89c5b3f6e77197cafeab256cc2e090284` | submission document |
| `../README.md` | `5ef44678e93f23f8286c507c29811f77e90274d334824f6336605e0ccd5b1c2f` | submission document |
| `../commit-messages/0001-arm-mach-hisilicon-luofu.txt` | `766990284609fe095e6d971fc1414972899aa2d5f8d0ff6048a9e1a4415b2ad5` | per-patch commit-message draft (provenance) |
| `../commit-messages/0002-arm-kconfig-textofs.txt` | `5f188ce552a4d5bd54cd98f031396c0f910d40f8ac9e566712711300a098ca5a` | per-patch commit-message draft (provenance) |
| `../commit-messages/0003-arm-textofs-nonimmediate.txt` | `76d40be5e780a9a2510fbbf50b0ed16294883881105dc22cea42e389e66e7dbe` | per-patch commit-message draft (provenance) |
| `../commit-messages/0004-arm-clk-reset-luofu-crg.txt` | `08ae227bbc11d30a1a113d9269c5eceb36b4ebfa07bd5867ba7cbf523551e6c3` | per-patch commit-message draft (provenance) |
| `../commit-messages/0005-arm-watchdog-luofu-wdt.txt` | `13804beec88400c806937a451b0ef6c401cd6170ad9f89b5e6681c1008ed58f3` | per-patch commit-message draft (provenance) |
| `../commit-messages/0006-arm-mtd-nand-luofu-fmc.txt` | `b9885546e4091e617de4a241212787d1eb0ed2cdb89171ca3c0321a300c99d36` | per-patch commit-message draft (provenance) |
| `../commit-messages/0007-net-mdio-luofu.txt` | `3aa7a5b4884f157bd1279f5bad9dac6c099b7a2499547a8b57173eb15614bd91` | per-patch commit-message draft (provenance) |
| `../commit-messages/0008-net-phy-luofu.txt` | `af2fd41fe88e1d25fecafefe26e53733830eae6dec0adc6301d71a6b3dfc1635` | per-patch commit-message draft (provenance) |
| `../commit-messages/0009-net-gmac-luofu.txt` | `e4825a2657d3da7fb92b4e3427aa28ce54a9446e59c5cf455a7c4daba1935c08` | per-patch commit-message draft (provenance) |
| `../commit-messages/0010-arm-luofu-restart.txt` | `8c62f18703a9f77758c3c49331b0e8c90d8193ca4e5551a51d271d7008f6ecb0` | per-patch commit-message draft (provenance) |
| `../commit-messages/0011-pinctrl-luofu.txt` | `f31c093a8433bbe366e9518d9923e3864d0bbc6d7faae3956fcbc4516f3305d6` | per-patch commit-message draft (provenance) |
| `../commit-messages/0012-arm-thermal-luofu-tvsensor.txt` | `4c139c2189584cfe3a87971143e958c4a6526c54349f2498c246c339461733bd` | per-patch commit-message draft (provenance) |
| `../commit-messages/0013-arm-pci-luofu-host.txt` | `0d42436d37c7797712c292ef52437b5bb41dc240bd2fe96aa3c068564bdd72dc` | per-patch commit-message draft (provenance) |
| `../commit-messages/0014-arm-crg-reset-polarity.txt` | `190929a3be8cd2ca0f05065c0561418bb0f7ae7b7cf55c9baf2e45f757644a3e` | per-patch commit-message draft (provenance) |
| `../commit-messages/0015-net-pie-luofu.txt` | `2aa337f236c8e9918e7ad979d206e7481d2d4a350b1508a5e4cc0c43c5e0f03d` | per-patch commit-message draft (provenance) |
| `../commit-messages/0016-net-lsw-luofu.txt` | `544c5dee8c51daa7be6c8c6efee343bda733530901612366114789781716c350` | per-patch commit-message draft (provenance) |
| `../commit-messages/0017-soc-luofu-boot-selector.txt` | `387fa0584fa72c63fa346ab5a6779640a895d582f7c505c8f3a67e01dc3717ea` | per-patch commit-message draft (provenance) |
| `../commit-messages/0018-arm-mach-hisilicon-smp.txt` | `3c6d80645d45417b3a1c817b97dc5c6b481585d82dc94aa7d4dba9fa0dbcdcba` | per-patch commit-message draft (provenance) |
| `../commit-messages/0019-arm-clk-luofu-clock-tree.txt` | `957ebc55793bd4ee4b04343fbb408cf0330f67441af5177b3eab7c8dbcbd7372` | per-patch commit-message draft (provenance) |
| `../commit-messages/0020-net-pie-datapath-luofu.txt` | `5b5491cc011a0507991e0adb17b3ba4e109da11f028ee3cfb8091a549a5d821b` | per-patch commit-message draft (provenance) |
| `../commit-messages/0021-net-lsw-init-luofu.txt` | `e5c7050afb38d4bb9f95f3f4489324e1b000d33cf4ff1efbaf979691fc820079` | per-patch commit-message draft (provenance) |
| `../commit-messages/0022-net-lsw-qidmap-write-luofu.txt` | `f0e486a5064482686dc9df1fb8ce1d7661436d77f2c3912cf800b9521ab31d2a` | per-patch commit-message draft (provenance) |
| `../commit-messages/0023-net-gmac-netdev-connect-luofu.txt` | `a4f982aff5e990243344996ff1b923912caa0a72f0bd739a8fac2075cbfec8be` | per-patch commit-message draft (provenance) |
| `../commit-messages/0024-net-pie-completion-decode-luofu.txt` | `4f0ed10cb5849f9ecc27eb523714f4a04c833f668a6773fd8b91fd88e5938814` | per-patch commit-message draft (provenance) |
| `../commit-messages/0025-net-lsw-egress-luofu.txt` | `18f99bae18c640b3b7c2e6248469f9e16c4de2eba8f20f2b75354c0249c74204` | per-patch commit-message draft (provenance) |
| `../commit-messages/0026-net-gmac-bdinfo-mac-nvmem.txt` | `bc31ff38703abd5e0c9e1d835943dfd41f5a3209448d4232beb07c1d915a6db8` | per-patch commit-message draft (provenance) |
| `../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` | `33956380db9ca5c7b91404596d6078d2f35e4a349978e41e4a1c5d5b9b5155fa` | DTS-series entry DTS-0001 (companion of 0018) |
| `../release-line/EXCLUDED.txt` | `35501a8366bb3404faaf6a3c3b0bb5bce684ebd6e36a2cee03a95a31b1b6895e` | release-line manifest/classification |
| `../release-line/INCLUDED.txt` | `cd7ae90c8791700661bf017742f7f46b3c66d12865fdadb05a7b9fab89351b2e` | release-line manifest/classification |
| `../release-line/manifest.tsv` | `9893116668b68d7f8bd3b52b9f3b9e7b2e2ac9e4e2a9be56e9b5e6cff45821fe` | release-line manifest/classification |
| `../series/0001-arm-mach-hisilicon-luofu.patch` | `c5eef73ce928b2e9f1bd0f397c3164d6b7a108fc930ccf7e537b184c81eee08b` | kernel review-form patch |
| `../series/0002-arm-kconfig-textofs.patch` | `bbb0266e2230d1c7e11ea616cd2f5591e1e3e043f042a62bc0c871f802597ad5` | kernel review-form patch |
| `../series/0003-arm-textofs-nonimmediate.patch` | `3bdcf2bcee6c8a6ceece7ca1ec357702230b979eae79578d0d67cc479c05befe` | kernel review-form patch |
| `../series/0004-arm-clk-reset-luofu-crg.patch` | `80f1e1887dd4e0068d2ac2b9c6362d89ad604bad62a00bd7271549318dbe2eae` | kernel review-form patch |
| `../series/0005-arm-watchdog-luofu-wdt.patch` | `3e1a13f5ecbe075876e382187cb3c50073f030d3cd113e1883445df425060129` | kernel review-form patch |
| `../series/0006-arm-mtd-nand-luofu-fmc.patch` | `115efee56fe3a8e693d379f9fd4c427130fec33657305dd66a7c584ea9a0fda2` | kernel review-form patch |
| `../series/0007-net-mdio-luofu.patch` | `6c9ddfb551d3160908632aea9d0cf866467d6c14830cd8e39cadc9f4443beda4` | kernel review-form patch |
| `../series/0008-net-phy-luofu.patch` | `7d655b2f8a4e87c721ede45c3c6706f6f3fc2d3b587737821f592940efaddee7` | kernel review-form patch |
| `../series/0009-net-gmac-luofu.patch` | `45a037f7b8e5861cebba16ab20aff66be0b2db929f749fbcc88db6a86cf0cf13` | kernel review-form patch |
| `../series/0010-arm-luofu-restart.patch` | `bfdcedfd920d64c0eccad29436cf1b3b9e589c4f10ba0d415929106a1e5ca2f8` | kernel review-form patch |
| `../series/0011-pinctrl-luofu.patch` | `872cc563004e20c18266f5f34979cb6e6880e6a2da84fcf436bfc5ffa3179a39` | kernel review-form patch |
| `../series/0012-arm-thermal-luofu-tvsensor.patch` | `131ed955ee95e7e513e701337b99a890e57b1bf27f1c9e15db6f9c22622075ea` | kernel review-form patch |
| `../series/0013-arm-pci-luofu-host.patch` | `45c47afd6fd83e5d8b5c5861f0715ec82a135c46c5058907668fb33ab53ada34` | kernel review-form patch |
| `../series/0014-arm-crg-reset-polarity.patch` | `8a8fba5b9f04e61c759d77a160caf31c32206ed882b97bf51c8882865f93e7eb` | kernel review-form patch |
| `../series/0015-net-pie-luofu.patch` | `152161c15017c515d4532c62ae244c28f3c56ec97d95accaea58e9be4107b663` | kernel review-form patch |
| `../series/0016-net-lsw-luofu.patch` | `f2872d6410f96350681297f6512b86e25f3d6062f127bcf5772d62d9855d4e28` | kernel review-form patch |
| `../series/0017-soc-luofu-boot-selector.patch` | `471a21225db7925d1e0b70f32f1637ee8e07842774ea6a6a22996988472c4da3` | kernel review-form patch |
| `../series/0018-arm-mach-hisilicon-smp.patch` | `b83933ee6b1aff92910ce5472ff428f261140f2a98fa8eed1f42cf2e6264479c` | kernel review-form patch |
| `../series/0019-arm-clk-luofu-clock-tree.patch` | `d97fd9aa2c2bc6a1110b4e88aaa851094647ce4b5084e2612e771d91e27f99e9` | kernel review-form patch |
| `../series/0020-net-pie-datapath-luofu.patch` | `624fa7cc19fcaa79d4d668c758d0216814863e99c561d775edba145ee3059212` | kernel review-form patch |
| `../series/0021-net-lsw-init-luofu.patch` | `d249a83a6da6c945cc7f8f588af1f2f2805467b84be7648aba469ed8f68f233e` | kernel review-form patch |
| `../series/0022-net-lsw-qidmap-write-luofu.patch` | `8c2b139a4f0b2bff8732363ba5962328bd09fad5e88ff4edeadb48696d157b53` | kernel review-form patch |
| `../series/0023-net-gmac-netdev-connect-luofu.patch` | `9119bb372977cc1d37e1328f8f110719959841148e8803c67882beb2262343f8` | kernel review-form patch |
| `../series/0024-net-pie-completion-decode-luofu.patch` | `5e93d7200543407841c201a4f6bc8decc664dce5680041f384b31ad44d6271b8` | kernel review-form patch |
| `../series/0025-net-lsw-egress-luofu.patch` | `507ce72495c09d104b0ad63ebac44ecf20a94a36e25f81b7739802f8fa005a00` | kernel review-form patch |
| `../series/0026-net-gmac-bdinfo-mac-nvmem.patch` | `099d7253f50c50ad18bce642a2d52fca7af37c9b1c7ac356038eafe3c7aa26df` | kernel review-form patch |
| `../tools/check-release-line.sh` | `cc6d768804170c978e2cd3661b51fb8f7db6390add960a8a0760da7d375751db` | validation/regeneration tool |
| `../tools/dts-upstream-filter.py` | `aec580281552669595f9279e3ec3309386e3e443171c5dec841c4bb9ce25b14b` | validation/regeneration tool |
| `../tools/gen-dts-series-patch.py` | `a85749398622774adacae0e7883a16d2cfd60b7b5a716992d7ce7026f6386d8e` | validation/regeneration tool |
| `../tools/gen-series.py` | `833efd29a9c8a6700cbbb4ba483a2195a5c19d5be1fca74d0aecda4c76e24f6f` | validation/regeneration tool |
| `../tools/run-validation.sh` | `460de5bc233b965140c4145de590074c0d2014b9a0dbb6747543bb1ca6046a25` | validation/regeneration tool |
| `../validation/checkpatch-default-summary.txt` | `23316e3bc770244d26394ad765c4ebf4db2d39887e08afb62f918de178fbb83b` | validation log |
| `../validation/checkpatch-default.log` | `c58992f31f9c0b523aaf994d69ea0a26d78e7fa7c56abd6f3e22aa2b66b55ff8` | validation log |
| `../validation/checkpatch-strict-summary.txt` | `b614a484a313bb81d7f79ae46d85a9d709f401405d0f8b018bd4d7c8f7102713` | validation log |
| `../validation/checkpatch-strict.log` | `08fe1f2fe1706c48c3d348d2cd3ae2385d34c5d1797d3127f7a18d96e53c343e` | validation log |
| `../validation/checkpatch-summary.txt` | `a39c08ea99036cc4a1e129f1b42e0888fcb9dcd6ef60f8dfe937c34d8f910587` | validation log |
| `../validation/checkpatch-version.txt` | `fd99169a93f914588cdc49c47aac9dc309dad95b95f44c6b5aab48e0b5504c0e` | validation log |
| `../validation/dt-binding-check.log` | `b64fa1f9d5e7d9cdcf528df491d33bd88d180dc2bfa26b5be9e6209ac30827e5` | validation log |
| `../validation/git-am-summary.txt` | `19da267e633c7ebe507b101ca05a4ba1ba55272f9b47a92002989046d23683fc` | validation log |
| `../validation/git-am.log` | `2672c0d1eabda0987b677df383c49674745345abb06b73bac1183cd603056de9` | validation log |
| `../validation/git-diffstat.txt` | `fde321ab9d2ef777c3de05922125c4c73fa52e62cde5cc6f011e4de900383e52` | validation log |
| `../validation/git-log.txt` | `e6e3c2ff158e929cbe2f73a7f45d065fd48cfed9b7b38b4d709d63262fc60b81` | validation log |
| `../validation/git-status.txt` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/offsets.txt` | `8140eb24235ed434a8db162121fd90e72c5fa9d5bc97c5b220c9704f26647423` | validation log |
| `../validation/patch-apply.log` | `48db9433a6cb504760c24af893c2d56b93cc3d34ab798dc953520c0b0ba31d09` | validation log |
| `../validation/release-line/dts-cpp.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-dtc.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-series-apply.log` | `766b86fe604a292ea75bef987b766dfe8974ccd1b6823b09b92a17f860c52670` | validation log |
| `../validation/release-line/dts-series-cpp.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-series-dtc.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-series-filter.log` | `d5afffe13ebd5b15122f493947d1ffd8b8e388852bbfcbb2cc635cda9c20ee67` | validation log |
| `../validation/release-line/excluded.txt` | `35501a8366bb3404faaf6a3c3b0bb5bce684ebd6e36a2cee03a95a31b1b6895e` | validation log |
| `../validation/release-line/git-apply-strict.log` | `41b89ee70893aefb5096489b7aa04063d85607cf13fb04190401e6020bbc4595` | validation log |
| `../validation/release-line/included.txt` | `cd7ae90c8791700661bf017742f7f46b3c66d12865fdadb05a7b9fab89351b2e` | validation log |
| `../validation/release-line/offsets.txt` | `75528687d76d4c88332abdb72d57e16488051387eab5c7992e37c4a3afb7b97c` | validation log |
| `../validation/release-line/pad-dtb.log` | `20f4b2c42dbf056cf1b8dcd67df469522a4d0b66c8e6275f19f562009e49589d` | validation log |
| `../validation/release-line/patch-apply.log` | `416b6ebe51aed884feb961f0beded1dbc514b403e9396fec4b6728b2fabbaf47` | validation log |
| `../validation/release-line/summary.txt` | `e1d0d7bab9ad745d1ebd47420ee07892af9181da604896bc44ce18281a0ca67b` | validation log |
| `../validation/sha256sums.txt` | `35c465bcad05a6d121eb21c8e58c71991617448bb4714c751f069ddd774a3849` | validation log |
| `../validation/tree-diff.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/tree-equivalence.txt` | `7a399c88d8c6aad4e39f3553c6c4b56986823f6c666bc87c35be41d74845b2a2` | validation log |
| `../validation/version.txt` | `fca6872c788884dd9df279d37e972d76924f2426be56259c9c451a47ae93f95e` | validation log |
| `README.md` | `5d61026019a1c20ff1663d4220cdfb309d493a6718d7c79aa18fbcfd90b63a69` | submission document |
| `TESTED-BY-POLICY.md` | `7ca6265ab90389437cab7b21ea4280aa50d8be467fca35b379b59579fb0e4071` | submission document |
| `cover-email-draft.md` | `2f01e0a34c28fbe4a1beb715849ccb3ea2cdd576fa4f0f22a1cb2e05d640b1b8` | submission document |

## Reproduce the validation

    # 1. pristine baseline (linux-6.18.60, sha256 3c2f44ef...; not stored in this repo)
    tar xf linux-6.18.60.tar.xz && cd linux-6.18.60
    # 2. kernel review form (mbox files carry mail headers)
    git am ../series/*.patch                    # 26/26, identical trees
    # or: patch -p1 -i each patch                # 26/26, 3 offsets, 0 fuzz
    # 3. release line (55 patches; drops the 11 bench-only SMP diagnostics)
    ../tools/check-release-line.sh --use-tree <pristine> <workdir>
    # 4. review-form replay + checkpatch + tree equivalence
    ../tools/run-validation.sh --use-tree <pristine> <workdir>
    # 5. DTS-0001 (separate DTS series; post with kernel patch 0018)
    #    applies to the release-line tree; its board DTS must be byte-identical to
    #    the RELEASE-LINE-FILTERED single source (tools/dts-upstream-filter.py strips
    #    the release-line-only W23 thermal delta; the validator enforces it).
    python3 ../tools/gen-dts-series-patch.py \
        --pristine <pristine> \
        --target-dts ../../../target-skeleton/dts/luofu-r116.dts \
        --out ../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch
    # 6. PCIe binding check (patch 0013; needs dtschema in PATH, dtschema 2026.9 used)
    make -C <patched-tree> ARCH=arm dt_binding_check \
        DT_SCHEMA_FILES=Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml
    # 7. regenerate the hash inventories, then verify this package's manifest
    #    (validation/sha256sums.txt: 93 entries; MANIFEST.sha256: 105 entries)
    (cd submission && sha256sum -c MANIFEST.sha256)

## Tested-by policy (summary; full text in TESTED-BY-POLICY.md)

There is **no upstream Tested-by, Reviewed-by, Acked-by or Signed-off-by from
any third party** in this package and none may be added.  The hardware evidence
is target-image bench evidence only (known-good image **7421bb7a**, 2 CPUs,
network/SSH/overlay, DTB byte-identical); it is not an upstream Test-by.  Host
checks (patch apply, dtc, contract, pad, checkpatch) are necessary but not
sufficient - the rebuilt image must be boot-tested on the unit after any
active-DTS change.

## ZERO remaining blockers (status 2026-11-21)

**All documented upstream blockers are closed.**  The last one - the
`hsan,flashinfo_reserved` loader hand-off binding - was closed by hardware
boot-test image **6e9c54d0**: the target DTS now uses
`compatible = "hisilicon,luofu-flashinfo", "hsan,flashinfo_reserved"` and the
stock loader accepted the documented first compatible (bootfip staging lines,
`atag_addr=0x80601000`, 2 CPUs, cmdline ubi.mtd=rootfsb + project marker, no
Oops/BUG/panic/RCU stall).  Evidence:
`build/evidence-bench-gates/flashinfo-loader/`; adopted in commit
`71b2167`.  The drafted reserved-memory binding stays as maintainer-facing
documentation (`../../../docs/incoming/upstream-blockers/01-flashinfo-reserved-memory/`).

**Closed by hardware boot-tests:** TWD level-high (f6685b80); MAC-nvmem removal,
switch MMIO-first order (DTS + in-series binding) and cudy-compatible ordering
(c3ef2a50); flashinfo compatible-first (6e9c54d0).  The remaining
draft-driver/YAML review items are normal RFC submission content, not blocker
tasks.  No hardware test remains for a documented blocker.

**Not claimed / out of scope:** pinctrl consumer states, consumer clocks, the
A/B boot-selector protocol beyond the measured cell, LED description, Wi-Fi and
thermal support, and unused vendor blocks (wired-first target).  Full record:
`../07-dts-upstream-bindings.md` section 6.

**Not claimed / out of scope (not counted as blockers above):** pinctrl consumer
states, consumer clocks, the A/B boot-selector protocol beyond the measured
0x10/0x11 cell, LED function/color descriptions, Wi-Fi and thermal support, and
the unused vendor blocks (wired-first target).

## Do not submit from this notebook

The owner must decide whether and where to send; this package intentionally has
no mailing-list transport.  Do not add a Tested-by/Reviewed-by that no
independent party gave, and do not present target-image/bench evidence as an
upstream Test-by.  This series is an RFC: ask for direction, not for merge.
