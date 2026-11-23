# wifi-h2d-catalog

Firmware-side H2D message catalog (shared task task-74), 2026-11-23. Host-only static analysis;
no device contact, no builds, no vendor binaries or firmware bytes committed.

- [REPORT.md](REPORT.md) - the catalog: mailbox layer ids 1/3/5/6; the new firmware HCC registration tables
  (group 2: 19 ids, group 0: 40 ids, group 0 early single id 0x2f) with every handler address and
  file offset; cross-reference with the vendor driver's H2D senders; payload layouts where
  inferable; and the recommended minimal-host send order (no HCC H2D frame is needed to boot; two
  safe liveness probes; never post mailbox id 5).
- [tools/fw_h2d_tables.py](tools/fw_h2d_tables.py) - locates the registrar and decodes the three registration tables;
  --verify asserts 20 byte-level claims against the image.
- [tools/h2d_join_vendor.py](tools/h2d_join_vendor.py) - joins the committed phase-37 vendor H2D send catalog with the
  firmware handler tables and prints the coverage table.
- [tools/payload_offsets.py](tools/payload_offsets.py) - heuristic per-handler payload-access extractor (probe-safety list,
  not a struct decoder).

Single-line answer: the firmware's H2D HCC receive map is static and fully enumerated - group 0
(40 handlers) covers every explicit vendor group-0 H2D sender 20/20, group 2 has 19 registered ids,
and the group-2/group-0 split for host group-2 sends is the one open anomaly (H1: dispatch keyed on
the destination-resource nibble; H2: core map not found), with a payload-free probe defined to
settle it. Closes the device-side H2D dispatch open item that wifi-h2d-inject (f64956f) and
wifi-host-replica (8978bd1) both listed; the stage-1 probe runs on the already-committed
injector with no new host code.
