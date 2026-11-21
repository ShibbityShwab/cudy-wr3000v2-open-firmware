# Upstream submission package - luofu 6.18 kernel series

**Status: DOCUMENTATION-ONLY PACKAGE / RFC.  Nothing has been submitted
anywhere.**  This directory is a dry-run hand-off bundle for the owner to review
and, if they choose, send manually; it contains no accounts, no email transport
and no list subscription.  Package form: **v3 submission-form revision**,
regenerated 2026-11-21 from the current tree (see git log for the exact commit).

## Current state (2026-11-21)

| item | value |
| --- | --- |
| kernel review form | 26 patches in `../series/` (RFC / do-not-merge split; see `../00-cover-letter.md`) |
| DTS-series entry | **DTS-0001** `../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`, sha256 `e9c9ed58b1a198bad16283d048a0498ee02297a8e1645f194df3f713c5d236b1` (713 lines) |
| single-source board DTS | pre-clean 1,329 lines -> conservative cleanup **691 lines** (7ae18f2) -> **700 lines** after the boot-tested TWD level-high delta (5b401c1); sha256 `13427d8e2bf7624b77fc6735932f79b9b25bf40dff1a0c859fc5cc3515b54de4` |
| compiled board DTB | **13,041 B** sha256 `4fde76df165689cdb859a6f79223300edc8fb27d1d239abea131d25726cbc1a9`; unaligned totalsize, `pad-dtb.py` pads to **13,048 B** sha256 `4b50de73bebf6241ccb1ac62907c1de9b49a7ec7bcc6f18286f638541456e890` |
| current DTS-purpose hardware image | **f6685b80** (boot-tested with the TWD level-high delta; 2 CPUs, `GIC-0 29 Level twd`, ping 20/20).  Previous verified baseline: **7421bb7a** |
| release-line validator | PASS: 51 include / 11 exclude, 51/51 patch(1) RC=0, 3 offset hunks, **0 fuzz**, strict git apply 51/51; DTS contract and DTS-0001 byte-identity OK |
| review form checks | 26/26 patch(1) and `git am`, identical trees, 3 offset hunks, 0 fuzz; checkpatch **0 errors / 58 warnings** |
| current devicetree final | submodule commit **5b401c1** (conservative cleanup 7ae18f2 + boot-tested TWD level-high delta); the superseded active cleanup **538377c** is regressed and NOT current (`../04-validation.md` sections 22-25, `../07-dts-upstream-bindings.md` section 6) |

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
| `../../../target-skeleton/dts/luofu-r116.dts` | `13427d8e2bf7624b77fc6735932f79b9b25bf40dff1a0c859fc5cc3515b54de4` | single-source board DTS (outside the package tree) |
| `../00-cover-letter.md` | `a9dd327957db4f577955435e61a900fb7d2c477a7cc086530177422067c7ea59` | submission document |
| `../01-commit-messages.md` | `d9fbfa51823d771ec1150358c716a53ea98a03d615529d60f5e0a2e3e4069ecd` | submission document |
| `../02-maintainers-proposal.md` | `30ae8a95b2786b3d60153f26fc4b8bf7b0c4b38b78e06e82e60b4a7f26d05c31` | submission document |
| `../03-rationale-tree-placement.md` | `7fdf3c34343a6004d69fc8cd55a055db02b0e471f363c91270e54889179350fa` | submission document |
| `../04-validation.md` | `8cf588add10a0ba46aa354d10284a793b899f211c876a14bebc79d9f3e57ff95` | submission document |
| `../05-release-line.md` | `1de59312259a712d29d1e9e780e8b44e41cae91b81c186992da3e2188815f2a9` | submission document |
| `../06-dts-series-dependency.md` | `cc0279e813087418fb8aea200c97ce33e18bde9dc979472db686736f3ebf8508` | submission document |
| `../07-dts-upstream-bindings.md` | `26ecc9487b270d25b79da25af776c762cd8ad78754ff98949d48771fb18fca88` | submission document |
| `../README.md` | `d463cee91023dfe4043ed4a9920b7bc81c03e8d23857671872504f92b6ecb842` | submission document |
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
| `../commit-messages/0013-arm-pci-luofu-host.txt` | `b8813bdba07276d31afb2a59f042552dd17886d857db82469363e2d8fc4e2c85` | per-patch commit-message draft (provenance) |
| `../commit-messages/0014-arm-crg-reset-polarity.txt` | `190929a3be8cd2ca0f05065c0561418bb0f7ae7b7cf55c9baf2e45f757644a3e` | per-patch commit-message draft (provenance) |
| `../commit-messages/0015-net-pie-luofu.txt` | `2aa337f236c8e9918e7ad979d206e7481d2d4a350b1508a5e4cc0c43c5e0f03d` | per-patch commit-message draft (provenance) |
| `../commit-messages/0016-net-lsw-luofu.txt` | `544c5dee8c51daa7be6c8c6efee343bda733530901612366114789781716c350` | per-patch commit-message draft (provenance) |
| `../commit-messages/0017-soc-luofu-boot-selector.txt` | `387fa0584fa72c63fa346ab5a6779640a895d582f7c505c8f3a67e01dc3717ea` | per-patch commit-message draft (provenance) |
| `../commit-messages/0018-arm-mach-hisilicon-smp.txt` | `3c6d80645d45417b3a1c817b97dc5c6b481585d82dc94aa7d4dba9fa0dbcdcba` | per-patch commit-message draft (provenance) |
| `../commit-messages/0019-arm-clk-luofu-clock-tree.txt` | `957ebc55793bd4ee4b04343fbb408cf0330f67441af5177b3eab7c8dbcbd7372` | per-patch commit-message draft (provenance) |
| `../commit-messages/0020-net-pie-datapath-luofu.txt` | `5b5491cc011a0507991e0adb17b3ba4e109da11f028ee3cfb8091a549a5d821b` | per-patch commit-message draft (provenance) |
| `../commit-messages/0021-net-lsw-init-luofu.txt` | `f783373b43c94012737f3632cae784a92acf6a6207f83af1c43847be5e1ebf05` | per-patch commit-message draft (provenance) |
| `../commit-messages/0022-net-lsw-qidmap-write-luofu.txt` | `f0e486a5064482686dc9df1fb8ce1d7661436d77f2c3912cf800b9521ab31d2a` | per-patch commit-message draft (provenance) |
| `../commit-messages/0023-net-gmac-netdev-connect-luofu.txt` | `a4f982aff5e990243344996ff1b923912caa0a72f0bd739a8fac2075cbfec8be` | per-patch commit-message draft (provenance) |
| `../commit-messages/0024-net-pie-completion-decode-luofu.txt` | `4f0ed10cb5849f9ecc27eb523714f4a04c833f668a6773fd8b91fd88e5938814` | per-patch commit-message draft (provenance) |
| `../commit-messages/0025-net-lsw-egress-luofu.txt` | `18f99bae18c640b3b7c2e6248469f9e16c4de2eba8f20f2b75354c0249c74204` | per-patch commit-message draft (provenance) |
| `../commit-messages/0026-net-gmac-bdinfo-mac-nvmem.txt` | `bc31ff38703abd5e0c9e1d835943dfd41f5a3209448d4232beb07c1d915a6db8` | per-patch commit-message draft (provenance) |
| `../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` | `e9c9ed58b1a198bad16283d048a0498ee02297a8e1645f194df3f713c5d236b1` | DTS-series entry DTS-0001 (companion of 0018) |
| `../release-line/EXCLUDED.txt` | `35501a8366bb3404faaf6a3c3b0bb5bce684ebd6e36a2cee03a95a31b1b6895e` | release-line manifest/classification |
| `../release-line/INCLUDED.txt` | `af5f32e40fd6bb7995205f90e4f5e2c796398c3fd8f15ba86d5f62c1305e9120` | release-line manifest/classification |
| `../release-line/manifest.tsv` | `4da8b960c3ad83939e0766a86969e8de2737a5b6eb79c91e80d28905f7030fd0` | release-line manifest/classification |
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
| `../series/0013-arm-pci-luofu-host.patch` | `8a8454f73c0c33f1c36e434ed463ac38e3155f79e4f082f2a679c2f53458fe88` | kernel review-form patch |
| `../series/0014-arm-crg-reset-polarity.patch` | `8a8fba5b9f04e61c759d77a160caf31c32206ed882b97bf51c8882865f93e7eb` | kernel review-form patch |
| `../series/0015-net-pie-luofu.patch` | `152161c15017c515d4532c62ae244c28f3c56ec97d95accaea58e9be4107b663` | kernel review-form patch |
| `../series/0016-net-lsw-luofu.patch` | `f2872d6410f96350681297f6512b86e25f3d6062f127bcf5772d62d9855d4e28` | kernel review-form patch |
| `../series/0017-soc-luofu-boot-selector.patch` | `471a21225db7925d1e0b70f32f1637ee8e07842774ea6a6a22996988472c4da3` | kernel review-form patch |
| `../series/0018-arm-mach-hisilicon-smp.patch` | `b83933ee6b1aff92910ce5472ff428f261140f2a98fa8eed1f42cf2e6264479c` | kernel review-form patch |
| `../series/0019-arm-clk-luofu-clock-tree.patch` | `d97fd9aa2c2bc6a1110b4e88aaa851094647ce4b5084e2612e771d91e27f99e9` | kernel review-form patch |
| `../series/0020-net-pie-datapath-luofu.patch` | `624fa7cc19fcaa79d4d668c758d0216814863e99c561d775edba145ee3059212` | kernel review-form patch |
| `../series/0021-net-lsw-init-luofu.patch` | `d20a2e676ce148396d7d71267fce120c06e983884aca7ff84b20b1f5e3815497` | kernel review-form patch |
| `../series/0022-net-lsw-qidmap-write-luofu.patch` | `8c2b139a4f0b2bff8732363ba5962328bd09fad5e88ff4edeadb48696d157b53` | kernel review-form patch |
| `../series/0023-net-gmac-netdev-connect-luofu.patch` | `9119bb372977cc1d37e1328f8f110719959841148e8803c67882beb2262343f8` | kernel review-form patch |
| `../series/0024-net-pie-completion-decode-luofu.patch` | `5e93d7200543407841c201a4f6bc8decc664dce5680041f384b31ad44d6271b8` | kernel review-form patch |
| `../series/0025-net-lsw-egress-luofu.patch` | `507ce72495c09d104b0ad63ebac44ecf20a94a36e25f81b7739802f8fa005a00` | kernel review-form patch |
| `../series/0026-net-gmac-bdinfo-mac-nvmem.patch` | `099d7253f50c50ad18bce642a2d52fca7af37c9b1c7ac356038eafe3c7aa26df` | kernel review-form patch |
| `../tools/check-release-line.sh` | `f0f7025cd18d6b6f7500d9bfc7e24dae39d1afd93051cc250aadc561ca5876dc` | validation/regeneration tool |
| `../tools/gen-dts-series-patch.py` | `17f971dc1ac2bd2f9837d8baeddf996ecc7cf4e1b45423170025ae838d2c5a71` | validation/regeneration tool |
| `../tools/gen-series.py` | `833efd29a9c8a6700cbbb4ba483a2195a5c19d5be1fca74d0aecda4c76e24f6f` | validation/regeneration tool |
| `../tools/run-validation.sh` | `460de5bc233b965140c4145de590074c0d2014b9a0dbb6747543bb1ca6046a25` | validation/regeneration tool |
| `../validation/checkpatch-default-summary.txt` | `30809589945efcc293a2d59130aeb3fc3a2d3b924ea91251557d04ea30d7d44e` | validation log |
| `../validation/checkpatch-default.log` | `c297d476734d9d90ffc406db19567d4fd54535b95fb8384526576b4c9218dd83` | validation log |
| `../validation/checkpatch-strict-summary.txt` | `a2732ceb427e9d7103524f1c7439e77103d7f4b290f06df03bac77e1c1e07f6d` | validation log |
| `../validation/checkpatch-strict.log` | `a44116402b26951171652687b2a0d23e7c05eebff7fcddf2f8cf7c51490bb085` | validation log |
| `../validation/checkpatch-summary.txt` | `f54741bf38811a5ab3a18d6c2a2d4af2ff88144a382469ffa8a5d7c56b2ed4b3` | validation log |
| `../validation/checkpatch-version.txt` | `92e929bf6fdae4a35e44eef54cf2dbcf4d465b241139574b3f57de488fd5818c` | validation log |
| `../validation/git-am-summary.txt` | `19da267e633c7ebe507b101ca05a4ba1ba55272f9b47a92002989046d23683fc` | validation log |
| `../validation/git-am.log` | `2672c0d1eabda0987b677df383c49674745345abb06b73bac1183cd603056de9` | validation log |
| `../validation/git-diffstat.txt` | `28e2c642cd3ff30b131350dd6a3bb2fb0ea331a1921c5bd7804ea4d56ba7df92` | validation log |
| `../validation/git-log.txt` | `221ad19073995773d101e1b3dec8aee0b915f5f4ba63af32dc8a49c57ec904ce` | validation log |
| `../validation/git-status.txt` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/offsets.txt` | `8140eb24235ed434a8db162121fd90e72c5fa9d5bc97c5b220c9704f26647423` | validation log |
| `../validation/patch-apply.log` | `48db9433a6cb504760c24af893c2d56b93cc3d34ab798dc953520c0b0ba31d09` | validation log |
| `../validation/release-line/dts-cpp.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-dtc.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-series-apply.log` | `766b86fe604a292ea75bef987b766dfe8974ccd1b6823b09b92a17f860c52670` | validation log |
| `../validation/release-line/dts-series-cpp.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/dts-series-dtc.err` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/release-line/excluded.txt` | `35501a8366bb3404faaf6a3c3b0bb5bce684ebd6e36a2cee03a95a31b1b6895e` | validation log |
| `../validation/release-line/git-apply-strict.log` | `a2986a59946d7e6fca451ef1cdd2e5e2ee0c7a55ebd9c5e7d72e62446a80297e` | validation log |
| `../validation/release-line/included.txt` | `af5f32e40fd6bb7995205f90e4f5e2c796398c3fd8f15ba86d5f62c1305e9120` | validation log |
| `../validation/release-line/offsets.txt` | `75528687d76d4c88332abdb72d57e16488051387eab5c7992e37c4a3afb7b97c` | validation log |
| `../validation/release-line/pad-dtb.log` | `b3d4e1d4ff93502db610be7cd6856fb65c93ffd8b356a446cda69d34ae4b070a` | validation log |
| `../validation/release-line/patch-apply.log` | `29c2b56b31433342b568973e8bd73c8c65bcb611b5aacdf8dab58820ceec06a1` | validation log |
| `../validation/release-line/summary.txt` | `d35ae39f1cfc794881c3860e852659587daf48d1fd50eb0fb4265e63a4e91aa5` | validation log |
| `../validation/sha256sums.txt` | `118c9e5065a73e64d0456677b04a1f286cef1cc9e37a8b5b2efc32f2b2a1fad8` | validation log |
| `../validation/tree-diff.log` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | validation log |
| `../validation/tree-equivalence.txt` | `7a399c88d8c6aad4e39f3553c6c4b56986823f6c666bc87c35be41d74845b2a2` | validation log |
| `../validation/version.txt` | `fca6872c788884dd9df279d37e972d76924f2426be56259c9c451a47ae93f95e` | validation log |

## Reproduce the validation

    # 1. pristine baseline
    tar xf linux-6.18.60.tar.xz                 # sha256 3c2f44ef... (see ../validation/version.txt)
    cd linux-6.18.60
    # 2. kernel review form (mbox files carry mail headers)
    git am ../series/*.patch                    # 26/26, identical-tree check
    # or: patch -p1 < each patch                 # 26/26, 3 offsets, 0 fuzz
    # 3. release line (51 patches, drops 11 bench-only SMP diagnostics)
    python3 ../tools/check-release-line.sh --use-tree <pristine> <workdir>
    # 4. review-form replay + checkpatch
    python3 ../tools/run-validation.sh --use-tree <pristine> <workdir>
    # 5. DTS-0001 (separate DTS series; post with kernel patch 0018)
    #    applies to the release-line tree; must stay byte-identical to
    #    ../../target-skeleton/dts/luofu-r116.dts (regenerated, never hand-edited)
    python3 ../tools/gen-dts-series-patch.py --pristine <pristine>         --target-dts ../../../target-skeleton/dts/luofu-r116.dts         --out ../dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch
    # 6. manifest
    (cd submission && sha256sum -c MANIFEST.sha256)

## Tested-by policy (summary; full text in TESTED-BY-POLICY.md)

There is **no upstream Tested-by, Reviewed-by, Acked-by or Signed-off-by from
any third party** in this package and none may be added.  The hardware evidence
is target-image bench evidence only (known-good image **7421bb7a**, 2 CPUs,
network/SSH/overlay, DTB byte-identical); it is not an upstream Test-by.  Host
checks (patch apply, dtc, contract, pad, checkpatch) are necessary but not
sufficient - the rebuilt image must be boot-tested on the unit after any
active-DTS change.

## Five remaining blockers before this could be merge-intent (status 2026-11-21); TWD closed

Blocker-reduction drafts (documentation + patch drafts only, not applied and not
part of this package) live under
`../../../docs/incoming/upstream-blockers/`; the table below points at them.

| # | blocker | status | draft |
| --- | --- | --- | --- |
| 1 | drivers + YAMLs are still drafts | open - prioritized top-issue review only | `05-driver-yaml-review.md` |
| 2 | `hsan,flashinfo_reserved` has no upstream binding | drafted reserved-memory child binding; maintainer direction needed; boot-test only if the vendor string is not first | `01-flashinfo-reserved-memory/` |
| 3 | `fixed-layout`/`mac-base` v6.19 on 6.18 | path defined: 6.18 uses the implemented userspace/vendor MAC provisioning; DTS nvmem removal drafted; boot-test | `02-mac-provisioning-6.18/` |
| 4 | switch queue-first `reg-names` / node name | drafted MMIO-first binding+DTS reorder; driver uses by-name so no code change; boot-test | `03-switch-binding-mmio-first/` |
| 5 | `cudy,wr3000-v2` compatible ordering + OpenWrt metadata | drafted DTS order + OpenWrt SUPPORTED_DEVICES/board_name diffs; boot-test + sysupgrade check | `04-compatible-order-openwrt/` |
| 6 | ~~TWD timer interrupt flags~~ | **CLOSED** - measured read-only PPI data + level-high delta boot-tested PASS on image f6685b80 (`/proc/interrupts`: `GIC-0 29 Level twd`, 2 CPUs, counts ~100 Hz, ping 20/20, no errors); adopted in commit 5b401c1 | `06-twd-irq-flags/` |

**What remains hardware-dependent:** MAC nvmem removal, switch reorder, and the
compatible ordering (boot-test + sysupgrade metadata check).  The flashinfo
binding is documentation-only unless the vendor compatible is no longer first;
the driver/YAML cleanups are code review.  Full record:
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
