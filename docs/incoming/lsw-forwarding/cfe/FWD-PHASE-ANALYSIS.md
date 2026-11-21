# Forwarded-traffic stock phase (2026-11-22): CFE populates L3/neighbour/tag tables, but the NAPT flow arenas stay EMPTY

Setup: stock vendor 5.10, second adapter in host netns 'wan' (192.168.50.1/24, dnsmasq DHCP + HTTP
server), root netns LAN 192.168.10.44, route 192.168.50.0/24 via 192.168.10.1; box WAN got
192.168.50.115/24; true forwarded traffic generated: ping -I 192.168.10.44 192.168.50.1 (5/5) and
curl --interface 192.168.10.44 http://192.168.50.1:8080/ (200) - LAN -> box -> WAN NAT.

Inputs in build/evidence-bench-gates/cfe-stock/ (sha256 in FWD.SHA256SUMS): fwd-carveout-a.bin
f65dc2f9..., fwd-carveout-b.bin 64496b73... (25 s later), fwd-win10-ifc 8955425..., fwd-win12-napt-a
a01f5abf..., fwd-win12-napt-b 771cd9f0..., fwd-win11-l2 7718e543..., fwd-win13-mcc b26c71a9...,
fwd-win15-pub 32ca3d39..., fwd-win16-pie 58b39fb8....

## 1. The NAPT v4/v6 DDR arenas are still EMPTY (negative, but now explained)

- v4 buckets (0x80400000), v4 arena (0x80408000), v6 buckets (0x80448000), v6 arena (0x80450000):
  **0 bytes changed** vs carveout-p0.bin and vs the routed phase; fwd a vs b also 0 changes.
  No bucket word, no 32 B or 88 B slot, no flow counter (napt+0x40000 bank still empty).
- The engine match counters are now non-zero: dn_match (0x1014) = 134, up_match (0x1018) = 91,
  ctrl_tocpu (0x101c) = 1, dn_tocpu (0x100c) = 27 - so the forwarded traffic did take engine
  match/punt paths, but it did **not** install a flow entry into the DDR tables.
- The a/b 25 s pair: match counters unchanged (134/91); tx_total/up_tocpu jump (3597->8221,
  3250->7868) with no match change - the counter banks are volatile interval/rotating gauges
  (consistent with all earlier phases), not cumulative counters.
- Interpretation: in this stock OpenWrt configuration the vendor CFE uses its software forwarding
  path plus the L3/neighbour/tag tables (below) and NAPT punt classes; the NAPT DDR flow cache is
  not used. Either hardware flow offload is disabled in this userspace/config, or flow entries live
  in engine-internal storage not visible via physdump. Either way, the 32 B/88 B slot encoding was
  not produced by any stock traffic phase so far.

## 2. What the forwarded traffic DID populate: the PUB window finally changed

fwd-win15-pub differs from p0 in exactly these tables (everything else byte-identical):
tagact entry 2, l3intf key/data entries 1 and 2, nhp entries 1 and 2. car/car_profile and mcc
userll are still unchanged/empty. ifc tab128 and its last engine descriptor are unchanged; its
counters move (rx_total/ifc_hit 8085, ifc_tocpu 113, ifc_napt 3178).

### tagact (pub+0x54000, 16 B rows)
    tagact[1] w0=0x00000000 w1=0x0000c000   (valid 0x80, selector 4)
    tagact[2] w0=0x00000000 w1=0x0000b000   (valid 0x80, selector 3)   <-- new with forwarding
Selector encoding from tag_write@0x2eb0: byte1 = 0x80 | (selector<<4); selectors 2..5 carry the
tag-add/del variant and selector 1..3 the delete/other variant depending on the mode bits; the
w0=0 rows carry no VLAN id and no 0x40000 flag. The pair is the interface tag action set; entry 2
is the one referenced by the new l3intf rows (below). Exact add/del enum names remain [I].

### l3intf (pub+0x5b000 key bank, pub+0x5c000 data bank; 16 B each)
Hardware layout decoded from l3intf_write@0x2bec (key: sip, byte-swapped MAC, flag byte;
data: tagact_id + mtu/ppp/ttl flags):

    l3intf[1] key=00000000 731c64ab 0dd4 02..  -> sip=0.0.0.0, mac=d4:0d:ab:64:1c:73, flags=0x02
              data=00000002 dc0d0c00...        -> tagact_id=2, mtu=1500 (0x5dc), bit18/19 set,
                                                 value 0x000c0ddc, statis_idx=0
    l3intf[2] key=7332a8c0 741c64ab 0dd4 03..  -> sip=192.168.50.115, mac=d4:0d:ab:64:1c:74, flags=0x03
              data=00000002 dc0d0c00...        -> tagact_id=2, mtu=1500, same flags

So the vendor wrote two L3 interfaces (the box LAN-side MAC ...:73 and WAN-side MAC ...:74; the WAN
one carries the DHCP address 192.168.50.115), both with MTU 1500 and both referencing the new
tagact entry 2. This is the first real hardware-table value set from stock and validates the
key/data split decoded from l3intf_write (sip in the key, MAC byte-swapped, tagact id in data
byte 3, mtu in data word 1 low 11 bits).

### nhp (pub+0x59000, 16 B rows) - next-hop/neighbour table
    nhp[1] = 2c0aa8c0 5817634c e000 b303 0010 1000
             ip=192.168.10.44, mac=00:e0:4c:63:17:58 (LAN host), hw flags words 0x03b3/0x0010/0x1000
    nhp[2] = 00000000 1402684c e000 c203 0020 1000
             ip=0.0.0.0, mac=00:e0:4c:68:02:14 (second/WAN host NIC), hw words 0x03c2/0x0020/0x1000

Layout from nhp_write@0x2d2c: +0..3 next-hop IP, +4..9 MAC (same byte-swapped encoding as l3intf),
+10..11 flags (0x100 marker + port/vport fields), +12..13 flags, +14..15 flags. These are the
vendor's ARP/neighbour entries for the LAN host and the WAN peer/gateway; the table is now real for
M2-B work.

## 3. M2 feasibility, final wording

- **M2-A (NAPT hardware flow cache): not feasible from the stock evidence and not worth pursuing for
  the release.** Even true forwarded NAT traffic leaves all four DDR arenas byte-identical and the
  flow-counter bank empty; no stock phase produced a flow entry, so the 32 B/88 B slot/engine write
  contract cannot be validated. Plausible reasons: hardware flow offload disabled in the stock
  OpenWrt userspace, or flow entries kept in engine-internal storage not CPU-visible. A controlled
  insert probe (W23) would be the only remaining route, and it would still need a verified engine
  write contract; recommendation: **do not implement M2-A**.
- **M2-B (L3/neighbour/tag table programming): data-backed but still not a release feature.** The
  forwarded phase gives the first real vendor values for l3intf/nhp/tagact, validating the static
  key/data layouts and the interface/neighbour/tag-action flow. However the actual forwarding still
  uses the software path plus NAPT punt; there is no demonstrated performance benefit, the IFC
  action engine is write-only and undecoded, and the engine/table contract is incomplete. Keep M2-B
  as documented feasibility (valuable reversal result), not an implementation lane. W22/M1b remain
  the release path; nothing here blocks it.

## 4. If more evidence is wanted (minimal next reads, in order)

1. **Cheap text state at the forwarded phase**: on stock after the same forwarded traffic, cat
   /proc/hsan/lsw/table/{ipv4_flow,ipv6_flow,flow_cnt,l3intf,tagact,userll,userll_ext} and
   /proc/hsan/cfe/table/{flow,mac_stats}, intf/dump_intf, hook/cnt, lrn/dump_lib. If any table shows
   alloc>0, the vendor decoder prints the entry fields and would settle the slot layout even though
   the DDR copy is empty (entries may be internal).
2. **Longer forwarded flow**: iperf3 TCP stream (10-20 s) or a persistent HTTP keep-alive through
   the box, then dump napt/carveout immediately; if a flow is ever installed, it will show here.
3. **Check whether offload is enabled**: lsmod | grep -E 'kcfe|kacc|kflow'; cat /proc/hsan/cfe/*,
   /etc/config/* hwnat/offload-related options, dmesg for CFE/acceleration init lines.
4. If nothing appears after 1-3, accept the conclusion: stock CFE does not populate the DDR flow
   cache in this userspace; M2-A is closed, M2-B remains documentation. No further stock phases are
   needed then.
