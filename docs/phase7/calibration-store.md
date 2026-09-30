# Calibration store: where the per-unit values actually live (phase 7, 2026-10-01)

The phase-7 dump-semantics report proved the calibration tuples are not in any hardware register
window. This note pins down where they are instead.

## The four files in `/usr/local/factory`

| file | size | what it is (verified) |
| --- | --- | --- |
| `wifi.cal` | 0 bytes | read by `/usr/bin/wifi_cal_init.sh` at boot; empty on this unit, so the init path tolerates a missing store |
| `hi5622v100.cal` | 13,487 bytes of text | the vendor script `hi5622v100_cal_get.sh` writes the **text output of `iwpriv Hisilicon0 alg get_*`** into it; the tuple words `17161605`, `0a0606ff` and `00006060` appear in it exactly as the live reads show |
| `wifi_cali_data.kv` | 8,912 bytes | the real machine-readable store: magic `ZZZZ`, then a little-endian u32 table (`0x9080` repeated seven times, then `0xa080` ...), ~61 percent zero words, trailer `0xa5a5a5a5` |
| `wifi_cali_data_2g.kv` | 2,336 bytes | the 2.4 GHz twin of the same format: `ZZZZ`, `0x8080` repeated six times, ~60 percent zeros, trailer `0xa5a5a5a5` |

Sha256 of the two stores as captured: `wifi_cali_data.kv` = `8b55839288a88eccf69aaa4366913e11acb0d45c9bea4254742f5fa184b57bbd`,
`wifi_cali_data_2g.kv` = `84dad372dd2e9c55ac268b6dae5e42bc5c23bfaa15eef60f466572519523221d`.

## Who owns the format

- The magic `ZZZZ` and the trailer `0xa5a5a5a5` do **not** appear as literals in either kernel module,
  so the file is not parsed by a simple constant in the driver binary; the driver's named writers are
  `hmac_save_cali_data_to_file_2g` / `_5g` (with `hmac_save_cali_data_verify_2g/5g`), and the strings
  `saving cali_data.kv file`, `save cali_data.kv file done` and `save cali_data_2g.kv file done` confirm
  the pairing. The format is therefore driver-defined, most likely assembled field by field.
- The text `.cal` file is purely a debugging dump produced by the vendor script; it is not the input the
  driver uses at boot (that path reads `wifi.cal`, which is empty here).

## The flow this implies

1. At manufacture (or first calibration run), the radio is calibrated and the driver writes
   `wifi_cali_data*.kv` into the factory partition.
2. At boot, the vendor init scripts apply calibration: either from `wifi.cal` (empty here) or from the
   `.kv` stores, which the driver reads and **sends to the firmware over the message channel** (the
   second half of the phase-5/6 envelope work).
3. The live values we read back through `alg get_*` are the firmware's copy, which is why they do not
   appear in any register window: they live in firmware RAM, delivered at boot.

## Why this matters for the open work

- A future open driver does not need to replicate the register-level calibration procedure to be useful;
  it needs to (a) read the `.kv` stores (format: magic, u32 table, trailer, ~8.9 KB and ~2.3 KB), (b)
  send them to the firmware with the documented envelope, and (c) read the results back with the same
  `alg` command ids we already mapped.
- The 26-word and 18-word tables read through `iwpriv` are the firmware's answer format, already
  documented word for word in `docs/phase2/power-decode.md` and `docs/phase6/message-fields.md`.

## Limits

- The field-by-field layout of the `.kv` tables is not decoded; only the container structure, the sizes,
  the zero fraction and the trailer are established. The writer functions in the driver are the place to
  continue from.
- No RAM read path has been confirmed yet; that is being probed separately and will be reported on its
  own.
