# incoming/qm-decode - round 4n: vendor-exact RX decode + accepterr (patch 053)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 19).  Prior: `032`...`051`.
This package delivers **patch 053**: the vendor-exact PIE RX completion decode (all three paths) and
the opt-in accepterr experiment, after W16 opened the RX path on silicon.  No device access.

Apply `shared.patch` from the `opensource` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Breakthrough: RX works after W16

Image 437a0766 (001-052, full W3-W16 + luofu_pie.rxtrace=1): W16 verified (gmac+0x1c=1,
gmac+0x100=0x33400600), then PIE RX ch0 completed 60-byte frames every ~1 s:
`pie-trace RXpoll ch=0 hw_rptr=1..6 sw_rptr=0..6 delta=1` and `RXCOMP ... len=60 status=37 port=0
desc=6000003c 824600c2 0100b125 00000380 00000000`.  Our decoder dropped all of them (status!=0,
port 0).

## 2. Decode: these are the host's ingress frames as switch exceptions

Vendor `hi_pie_rx_get_desc@0xf14` exact semantics:
- status = byte[+8] & 0x7f; **nonzero = the exception/error path**, which clears the no-error
  summary bit and is dropped by `hi_net_rx_proc` in the vendor stack;
- on the error path **port = byte[+9] bits 7:4**, vport = byte[+24] bits 4:0;
- normal path (status 0): port = byte[+16] (vport byte[+17]); when +16 is zero:
  port = byte[+23] bits 7:4, vport = byte[+24] bits 4:0;
- tag class = byte[+10] bits 5:4, second flag class = byte[+13] bits 6:5 - neither decides error.

For the observed completions: status 0x25 = 37 (exception code [U]), **error-path port = 0xb = 11 =
eth2**, vport 0.  So the 60-byte 1/s frames are the host's ping ARP requests arriving at eth2 and
punted to the CPU as exceptions because the switch has no CFE L2/VLAN forwarding state for them -
they are not switch management frames and not CPU port 0.

## 3. Patch 053 = vendor-exact decode + experiment

- `luofu_pie.rxdiag=1@BT@: full 32-byte descriptor plus decoded fields for the first 24 completions.
- `luofu_pie.accepterr=1@BT@: **experiment** - deliver exception completions to the net_device
  registered for the decoded error-path port (11 = eth2) instead of dropping them (the vendor stack
  drops them).  This tests whether the wire->CPU->wire loop can complete without the missing CFE
  tables: if the kernel answers the ARP, the remaining gap is that L2/VLAN state; if not, the TX
  direction is next.
- Default behaviour stays vendor-faithful (exception frames dropped, counted in rx_errors).

## 4. Expected bench lines

```
pie-trace RXDIAG ch=0 idx=0 len=60 status=37 err=1 port=11 vport=0 vlan=0 desc=...
pie-trace RXERR ACCEPTED (accepterr=1, vendor would drop) status=37 port=11 vport=0 len=60 ch=0
```

## 5. Recommended next tests (same image)

1. `luofu_pie.accepterr=1 luofu_pie.rxdiag=1@BT@ + full chain: run the host ARP/ping.  Expect RXERR
   ACCEPTED lines with port=11; check whether the host receives an ARP reply.
2. Targeted ingress while watching RXDIAG: unicast frame to the box MAC, broadcast ARP, and a
   VLAN-tagged frame - if any completes with a different error path port or normal status, that
   tells us which classification state is missing.
3. If ARP replies are generated but never leave, the TX direction is the next decode (W16 may already
   have fixed it; the host sniff will tell).

## 6. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/053-net-pie-rxdecode-vendor-exact.patch` | vendor-exact RX decode + rxdiag + accepterr | `56d4ae6c2e97e93ab3076c9849c69c4da2456a657e36d86008706f3edbf881c6` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | all pending docs (W10..W17) | `d5d8d2e82e346d503499130ba4b4f9a284a6ffc78bfa2e385ec50246df6258b3` |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 7. Validation

- Series apply: all patches apply, 0 failed hunks, 0 .rej/.orig (`logs/10-series-apply-053.log`); the
  final whitespace-only revision of the patch was recompiled and checkpatched.
- WERROR compile clean; checkpatch 0 errors (`logs/11-checkpatch-053.log`).
- No devicetree/binding/Kconfig change.
