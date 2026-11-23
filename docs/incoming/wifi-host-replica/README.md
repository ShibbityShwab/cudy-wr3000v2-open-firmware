# wifi-host-replica — vendor host-driver post-release behaviour and our minimal counterpart

**Lane:** host-replica (host-only). **Written:** 2026-11-23. **Status:** static reversal complete;
skeleton compiles against the 6.18.60 tree; no bench action taken by this lane.

**Task:** from the vendor `hi5622v100_plat.ko` / `hi5622v100_wifi.ko` (2.5.24 and 2.4.15)
extract the complete host-side post-release behaviour: every H2D message, the D2H service
model, the HCC init/registration sequence, the host-side objects the firmware expects, and the
ready-message wait logic — then define the minimal host counterpart we are missing and write a
compile-ready C skeleton against our transport module sources.

**Deliverables**

| path | content |
| --- | --- |
| REPORT.md | full reversal: evidence, tables, call sites, corrections to earlier reports, unknown list |
| SPEC.md | minimal implementable host counterpart: state machine, H2D frames, service-loop timing, acceptance criteria |
| skeleton/hi5622_host_replica.h | replica interface |
| skeleton/hi5622_host_replica.c | compile-ready C skeleton (state machine + D2H service + HCC dispatch + gate ack + heartbeat) |
| skeleton/Kbuild.snippet | one-line Kbuild integration into the wifi-transport-port build |
| skeleton/BUILD-NOTES.md | build line, compile result, integration instructions |
| tools/ | host-only regeneration tools: relocation-aware disassembly annotator, call-site census, table dumper |

**Scope and rules honoured:** host-only work, no bench contact (no 192.168.1.1 / 192.168.10.1),
no image or OpenWrt build; no vendor binaries, object code, source text, firmware bytes or
secrets are committed — only original analysis text, addresses/ids and original code. The vendor
modules and the firmware blob are local evidence referenced by path only.

**Headline findings (details + evidence in REPORT.md)**

1. The vendor does **not** write the 0xcece gate anywhere (independent scan of all 60+ vendor
   `.ko`: 0 hits). The release is a single 4-byte `bal_write` of 0x5a5a to CA 0x40000108 (a
   descriptor `{CA, src=&0x5a5a, len=4}` in `.data+0x26a4`), issued as the "quit send" at the end
   of `firmware_download()`. Host IRQ enable follows it. Nothing else is sent before the device's
   first HCC message.
2. The **real** device-ready handshake is two HCC chip-group-4 payloads (id 1 then id 2), waited
   on with 200-jiffy (2.0 s) and 2000-jiffy (20 s) completions at HZ=100. The bench's `bitmap=0x4`
   is D2H **mailbox id 2 = EXCP_PANIC**, not "device ready": the handler table of the fully-loaded
   driver registers ids {1,2,3,4,5,6,7}, not {1,3,6,7} as previously reported. The routine that
   posts it is the non-reset vector handler decoded by the earlier fw-ready-gate lane: it zeroes
   CA 0x4000010C/0x40000108, announces `out[1]=4` + D2H doorbell, then spins for 0xcece. A
   single-shot host 0xcece is consumed by the first vector entry — the missing counterpart is an
   **event-driven gate acknowledgment per announced vector entry**, not one write at release.
3. The minimal host counterpart is receive-side: full D2H mailbox service (ack → drain → re-arm →
   dispatch, exact order), the HCC group-4 boot table, the 100-byte `g_dmac_to_hmac_read_msg`
   payload consumer, the two ready completions, the ETE DR drain that carries those frames, the
   exception-context capture (vector mask CA 0x40008 + 17-word frame CA 0x4000C..0x40044), and the
   10 s heartbeat restart. H2D traffic before READY is exactly one data write (release) plus the
   gate magic; the first mailbox send is id 3 (SR announce), emitted by the first HCC transmit.
4. 2.4.15 and 2.5.24 are semantically identical for the whole boot/ready/D2H path (same tables,
   same registration ids, same 200/2000 jiffies, same log strings); the 2.4.15 module merely keeps
   names that 2.5.24 inlined (`g_multi_chip_init_res`, `g_dmac_to_hmac_read_msg`,
   `get_plat_ready_flag`, `get_device_ready_flag`, `g_quit_cmd`, `g_file_info`, `g_dump_meminfo`,
   `h2d_write32/read32`, ...). 2.5.24's symbol set is a strict subset of 2.4.15's.
