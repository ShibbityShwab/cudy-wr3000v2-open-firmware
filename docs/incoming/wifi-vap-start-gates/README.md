# wifi-vap-start-gates - the G9/G11 gate semantics (settled)

**Lane:** wifi-vap-start-gates (shared task **task-212**), 2026-11-28.
**HOST-ONLY:** no device contact, no fire. **The Lead fires.**
**Question:** the R4b fire's own G9 gate refused the key-0x0065 VAP start
('vap_start_g0 REFUSED (-16): G9: mctx[0x440] band-config flag is set') even
though the order is correct and the RF is programmed. Was the gate right?

## Verdict

**The G9/G11 gates are wrong. Both bytes are the RF-enable / band-up flags:
1 = normal, 0 = the 5 GHz RF programme (CA 0x40684 -> 0x101890) is silently
skipped.** The key-0x0065 worker CA 0x46394 never reads either byte; the only
firmware consumer, CA 0x944DE, requires **both == 1** to let the RF programme
run. The R4b fire's own probes prove both were 1 at the ap_chan post (the RF
image R+0x3AC = 0x03E6138A was written), so G9 refused a *normal* state.

The corrected gates are inverted: **G9' u8[mctx+0x440] == 1** and
**G11' u8[bandobj+0xEC8] == 1**, refusal only when 0 ("RF programme
disabled/skipped - no beacon can air"). Everything else (the R4b order, the
0x0065 frame, G5/G7/G8/G10/G12/G13) is unchanged.

## The artefacts (committed local, uncommitted vendor bytes)

| artefact | pin |
| --- | --- |
| firmware | local `recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`, CPU = file offset + 0x40000 |
| vendor module | local `recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko`, 3,581,748 B, sha256 `22fa789a...c7d2` (unstripped; symbol values are .text-relative) |
| objdump | the OpenWrt lab ARM objdump (`--objdump`, `$VSG_OBJDUMP`) |

The tool `tools/fw_vap_start_gates_re.py` re-derives every claim
(`--selftest` = 39 checks, `--decode`, `--tables`); evidence in
`evidence/`. No vendor byte is copied into the repository.

See **REPORT.md** for the decode, **TABLES.md** for the field/gate matrices,
**FIRE-RECIPE.md** for the corrected fire recipe, **BUILD-NOTES.md** for the
host-built gate-inverted module and
`patches/0001-g9-g11-rf-enable-invert.patch` (module) +
`patches/0002-gate-selftest-update.patch` (host test).

## Host-built artifact (local evidence only)

Reproduce with `tools/build-gates.sh` (base /tmp/r4border-build + patch 0001):

    /tmp/vsg-artifact/hi5622_wifi.ko  382,376 B
    sha256 9bf1d846e730ce57a5adfef9a2fed9ab589fcba159b6cd08165339917673cf82
    65 params, 0 warnings; C selftest 293/0 + Python R4b-order 93/0.
    NOT the fired R4b artifact (382,188 B / b9926c1c).
