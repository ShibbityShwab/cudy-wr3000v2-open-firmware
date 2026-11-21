# Hi5622V100 firmware external interface — SUMMARY

**Lane:** `wifi-firmware-re` (host-only, read-only). **2026-11-21.** Full detail: `REPORT.md`.

## One-line verdict

The Hi5622V100 firmware (`FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20…`, banner
`ChenTangV100R001C20T13`) is a **closed on-device ARMv7 (Thumb-2) program that cannot be clean-room
reimplemented**; the transport/handshake is fully decoded, but the firmware's MAC/MLME behavior over the
H2D/D2H message protocol is not, and never was, recoverable by black-box means. **The only path to a
working driver is host-side GPL source, cross-checked against the contract below.**

## What is now pinned down (offsets / tables / handshake)

| area | facts |
| --- | --- |
| blob | monolithic, no ELF, uncompressed (entropy 7.2 code / 5.5–7.0 data); size `0xE2C98` |
| header | `0x00` entry `0x46971` (Thumb @ file 0x46970), `0x04` data table `0xC742D`; 0x08–0x4F zero |
| vector table | `0x50`: 5 pointers `0x00103xxx` → file 0xC3090–0xC3140 (5 ARM exception stubs) |
| code/data | code 0x64–0xC81FF (ARM stubs then Thumb-2 body); string/debug 0xC8400–0xCF9FF; tables 0xCFC00–0xE2C97 |
| banner | `ChenTangV100R001C20T13` @ 0xCD2EC; HCC TX/RX/OAM Threads @ 0xCCFC8/0xCCFF8/0xCD028 |
| symbol tables | `smac_*_isr` @ 0xE266C; `smac_msg_proc_*` H2D handlers @ 0xE2824; lock/state names @ 0xC4188–0xC6D3D |
| MMIO init table | `{CA,value,0}` triplets @ 0xE2524–0xE2594 (0x40034000/0x40035000←0x3A/0x3B, 0x40107000←0x3A); count 0x0F + `DEADBEEF` @ 0xE259C |
| load target | contiguous `memcpy_s` to device CA `0x01240000` (BAR0+0x6f8000, alias BAR0+0x40000), chunks of 0x80000 |
| iATU | 6 inbound regions (`g_shuangta_region_types`); BAR2 `iatu_bar1` +0x104+0x200*i; `PCI_COMMAND=7` |
| boot control words | **0x5a5a → CA 0x40000108** (CPU release); **0xcece → CA 0x4000010c** (D2H gate) |
| ready messages | HCC id 1 "Device plat ready!" then id 2 "DEVICE READY" (200/2000-jiffy waits); first mailbox word `out[1]=0x4` ~1.85 s post-kick |
| mailbox | H2D mask `0x40039010`, ready `0x40039014`, doorbell `0x400392d4`, MAC-side `0x40101414`/`0x40101438`; D2H doorbell `0x40101434`; **never write `0x400392f0`** |
| ETE rings | block `0x4003a000`, 7 channels × 0x50 (SR0–2 @ +0x400/450/4a0, DR0–3 @ +0x590/5e0/630/680); node 8 B `{devva, (len<<16)|0x6d2b}` |
| status words | 11 `dev_status_check` registers (efuse_chip_id 0x186, dcoldo_vset, pbank/abank, temp, lock, pcie0_status, tcxo_pll_*) |

## Honest verdict (for the report and the GPL request)

1. The firmware is a **closed on-device program**; no clean-room reimplementation is possible — the
   device-side `smac_msg_proc_*` names are readable debug symbols, but their wire encoding, ordering and
   timing are undocumented, and the prior from-scratch takeover never reached a working data path
   (H2D "line 0x4C / SGI bank" gate closed, phase22–27/49).
2. The **only route** is host-side GPL source (both vendor modules declare `license=GPL`, un-stripped) +
   this interface contract; a 6.18 port is 10–16 engineer-weeks after source, and upstream-impossible.
3. The 9-item cross-check list in `REPORT.md` §5 is what the GPL source must be verified against.

## Redaction / scope

No firmware or calibration blob, no MAC/serial/die-id/PIN, no per-unit value is committed. All facts are
re-derivable from named local artifacts. This lane performed **no device contact, no module load, no
build, and no write to any vendor artifact**.
