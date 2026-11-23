# wifi-boot-kick

Vendor post-release boot-kick reversal (host-only), 2026-11-23.

- [REPORT.md](REPORT.md) — reconstructed vendor boot sequence, evidence table,
  answers to the task questions, diff vs our transport, ranked candidates and
  the full C1 first-test procedure.
- [tools/c1-gate-hold.sh](tools/c1-gate-hold.sh) — next-session driver for
  candidate C1 (sustained `0xcece` gate service after the `0x5a5a` release).
- [tools/vendor_const_scan.py](tools/vendor_const_scan.py) — host-only constant
  scanner that reproduces the negative claims (no `0xcece`, no firmware-tail
  CA writes in any vendor module).

Single-line answer: the firmware's vector-entry routine zeroes
CA 0x4000010C at file 0x86E7E and only then spins on it (file 0x86F78), so the
host's single post-release `0xcece` write is always destroyed; the gate has to
be held/served while the firmware is up (mask 0x08 -> 0x1E).

No vendor binaries, firmware bytes or secrets are tracked here.
