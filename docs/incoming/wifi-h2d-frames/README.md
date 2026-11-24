# wifi-h2d-frames - exact H2D frame catalogue, builders and validators

Host-only deliverable of the wifi-h2d-frames lane (shared task task-93, 2026-11-24).
It turns the committed vendor/firmware reversal into a compile-ready C library:
the 12-byte HCC header contract, byte-exact builders for the frames whose payload
layout is decoded (including the vendor first post-ready group-0 id-1 TXQ config
sync), a refusal/policy validator driven by the firmware H2D receive tables, the
SR node + packed producer + mailbox announce model, and a device->host ready-frame
decoder - plus a 233-check device-free selftest and a small frame CLI.

Start with REPORT.md. Build/test:

```
./tools/build.sh          # catalogue --check + host compile -Werror + 233-check selftest
./tools/kernel-check.sh   # optional kernel 6.18.60 module compile check (host-only)
```

No device contact, no image build, no vendor artifact read by the shipped tools.
