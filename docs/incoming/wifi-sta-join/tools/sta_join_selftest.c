// SPDX-License-Identifier: GPL-2.0
/*
 * sta_join_selftest.c - host-side tests for the wifi-sta-join lane.
 *
 * Builds the portable sources (hi5622_sta_logic.c + hi5622_sta_frames.c)
 * with a normal host compiler and asserts:
 *   T1  cfg80211 connect-request bounds (ssid/ie/key/auth/key_idx)
 *   T2  one-shot join gate + firmware health gate (mask/staging stop)
 *   T3  the four 108-byte-body host-DRAM address fields: layout, window,
 *       alignment, region bounds, no 32-bit wrap, no-NULL policy
 *   T4  the 108-byte connect body + 140-byte WAL frame wire layout
 *   T5  the 16-byte disconnect body + 48-byte frame
 *   T6  D2H WAL g2 id3 assoc-completion decode (incl. untrusted-address
 *       rejection) and g2 id4 disassoc-completion decode
 *   T7  vendor band/HT/VHT constants stay as decoded
 *
 * No kernel headers, no device access, no vendor byte.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_sta_frames.h"
#include "hi5622_sta_logic.h"

static int checks;
static int fails;

#define CK(cond, ...) do { \
	checks++; \
	if (!(cond)) { \
		fails++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

#define DMA_LO HI5622_HOST_DMA_LO_TEST
#define DMA_HI HI5622_HOST_DMA_HI_TEST

/* identity window used by the bench-proven SR/DMA path */
#define HI5622_HOST_DMA_LO_TEST 0x80000000u
#define HI5622_HOST_DMA_HI_TEST 0x8fffffffu

static const u8 test_bssid[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 test_ssid[5] = { 't', 'e', 's', 't', '1' };

static void fill_bufs(struct hi5622_sta_join_bufs *b, u32 buf_va)
{
	memset(b, 0, sizeof(*b));
	b->bssid_va = buf_va + HI5622_STA_JOIN_OFF_BSSID;
	b->ssid_va = buf_va + HI5622_STA_JOIN_OFF_SSID;
	b->ie_va = buf_va + HI5622_STA_JOIN_OFF_IE;
	b->key_va = buf_va + HI5622_STA_JOIN_OFF_KEY;
	b->ssid_len = (u8)sizeof(test_ssid);
	b->mfp = 0;
	b->privacy = 0;
	b->auth_type = 0;
	b->key_len = 0;
	b->key_idx = 0;
	b->ie_len = 0;
}

static void put16(u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static void d2h_hdr(u8 *hdr, u16 id, u16 payload_len)
{
	memset(hdr, 0, HI5622_STA_D2H_HDR_LEN);
	hdr[0] = HI5622_STA_D2H_WAL_GROUP;
	put16(hdr + 0x04, (u16)(HI5622_STA_D2H_HDR_LEN + payload_len));
	put16(hdr + 0x06, id);
	put16(hdr + 0x0a, HI5622_STA_D2H_TAG);
}

/* ---------------- T1: connect request bounds ---------------- */
static void t1_request_bounds(void)
{
	struct hi5622_sta_connect_check c;
	u8 key[32], ie[16];

	memset(key, 0xaa, sizeof(key));
	memset(ie, 0xdd, sizeof(ie));
	memset(&c, 0, sizeof(c));
	c.ssid = test_ssid;
	c.ssid_len = sizeof(test_ssid);
	c.bssid = test_bssid;
	c.auth_type = 0;
	CK(hi5622_sta_connect_check(&c) == 0, "valid request rejected");
	CK(hi5622_sta_connect_check(NULL) == -EINVAL, "NULL check accepted");

	c.ssid_len = 0;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "ssid_len 0 accepted");
	c.ssid_len = 33;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "ssid_len 33 accepted");
	c.ssid_len = sizeof(test_ssid);
	c.ssid = NULL;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "NULL ssid accepted");
	c.ssid = test_ssid;

	c.ie = ie;
	c.ie_len = 513;
	CK(hi5622_sta_connect_check(&c) == -E2BIG, "ie_len 513 accepted");
	c.ie_len = 8;
	c.ie = NULL;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "ie_len>0 + NULL ie accepted");
	c.ie = ie;
	c.ie_len = 0;

	c.key = key;
	c.key_len = 33;
	CK(hi5622_sta_connect_check(&c) == -E2BIG, "key_len 33 accepted");
	c.key_len = 5;
	c.key = NULL;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "key_len>0 + NULL key accepted");
	c.key = key;
	c.key_len = 0;

	c.auth_type = 9;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "auth_type 9 accepted");
	c.auth_type = 0;
	c.key_idx = 4;
	CK(hi5622_sta_connect_check(&c) == -EINVAL, "key_idx 4 accepted");
}

/* ---------------- T2: gates ---------------- */
static void t2_gates(void)
{
	CK(hi5622_sta_join_gate(0, 16, 0) == -EOPNOTSUPP, "sta_join=0 not refused");
	CK(hi5622_sta_join_gate(3, 16, 0) == -EINVAL, "sta_join=3 accepted");
	CK(hi5622_sta_join_gate(1, 0, 0) == 0, "dry run requires allow_frames");
	CK(hi5622_sta_join_gate(1, 0, 1) == 0, "dry run latched by attempt");
	CK(hi5622_sta_join_gate(2, 0, 0) == -EPERM, "post without allow_frames accepted");
	CK(hi5622_sta_join_gate(2, 16, 0) == 0, "post refused with allow_frames");
	CK(hi5622_sta_join_gate(2, 16, 1) == -EALREADY, "second post not refused");

	CK(hi5622_sta_health_gate(0, 1, 0) == 0, "clean health refused");
	CK(hi5622_sta_health_gate(0, 0, 0) == 0, "unlatched health refused");
	CK(hi5622_sta_health_gate(1, 1, 0) == -EIO, "mask != 0 not stopped");
	CK(hi5622_sta_health_gate(0x8000, 1, 0) == -EIO, "mask bit not stopped");
	CK(hi5622_sta_health_gate(0, 0, 3) == -EIO, "staged vector not stopped");
}

/* ---------------- T3: body-address validation ---------------- */
static void t3_addresses(void)
{
	const u32 buf = 0x81234000u;	/* inside the identity window */
	struct hi5622_sta_join_bufs b;
	int i;
	u32 *ptr[4];

	CK(hi5622_sta_join_bufs_check(NULL, buf, 584, DMA_LO, DMA_HI) == -EINVAL,
	   "NULL bufs accepted");
	fill_bufs(&b, buf);
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == 0,
	   "valid bufs rejected");
	CK(hi5622_sta_join_bufs_check(&b, buf, 583, DMA_LO, DMA_HI) == -ENOSPC,
	   "short buffer accepted");
	CK(hi5622_sta_join_bufs_check(&b, 0x40000000u, 584, DMA_LO, DMA_HI)
	   == -ERANGE, "buffer outside window accepted");
	/* whole buffer must stay inside the window */
	CK(hi5622_sta_join_bufs_check(&b, 0x8fffff00u, 584, DMA_LO, DMA_HI)
	   == -ERANGE, "buffer crossing window end accepted");
	/* ssid_len bounds */
	b.ssid_len = 0;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -EINVAL,
	   "ssid_len 0 accepted");
	b.ssid_len = 33;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -EINVAL,
	   "ssid_len 33 accepted");
	b.ssid_len = 5;
	b.ie_len = 513;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -E2BIG,
	   "ie_len 513 accepted");
	b.ie_len = 0;
	b.key_len = 33;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -E2BIG,
	   "key_len 33 accepted");
	b.key_len = 0;
	b.key_idx = 4;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -EINVAL,
	   "key_idx 4 accepted");
	b.key_idx = 0;
	b.auth_type = 9;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -EINVAL,
	   "auth_type 9 accepted");
	b.auth_type = 0;

	ptr[0] = &b.bssid_va;
	ptr[1] = &b.ssid_va;
	ptr[2] = &b.ie_va;
	ptr[3] = &b.key_va;
	for (i = 0; i < 4; i++) {
		u32 save = *ptr[i];

		*ptr[i] = 0;
		CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI)
		   == -EINVAL, "zero address %d accepted", i);
		*ptr[i] = 0x40000000u + HI5622_STA_JOIN_OFF_BSSID;
		CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI)
		   == -ERANGE, "outside-window address %d accepted", i);
		*ptr[i] = 0x81234001u;
		CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI)
		   == -EINVAL, "misaligned address %d accepted", i);
		*ptr[i] = save;
	}
	/* fixed layout is enforced */
	b.key_va = b.ssid_va;
	CK(hi5622_sta_join_bufs_check(&b, buf, 584, DMA_LO, DMA_HI) == -EINVAL,
	   "swapped layout accepted");
	fill_bufs(&b, buf);
	/* region bounds: force ie_len max, buffer too small for it */
	CK(hi5622_sta_join_bufs_check(&b, buf, 100, DMA_LO, DMA_HI) == -ENOSPC,
	   "undersized buffer accepted");
}

/* ---------------- T4: connect body + WAL frame ---------------- */
static void t4_connect_frame(void)
{
	const u32 buf = 0x81234000u;
	struct hi5622_sta_join_bufs b;
	struct hi5622_sta_disconnect_params dp;
	u8 body[HI5622_STA_CONNECT_BODY_LEN];
	u8 frame[HI5622_STA_FRAME_MAX];
	size_t flen = 0;
	int rc;

	fill_bufs(&b, buf);
	b.key_len = 5;
	b.key_idx = 1;
	b.privacy = 1;
	b.auth_type = 3;
	b.mfp = 2;
	b.ie_len = 8;
	rc = hi5622_sta_connect_body(body, sizeof(body), &b);
	CK(rc == 0, "connect body build failed: %d", rc);
	CK(sizeof(body) == 108, "connect body size %zu", sizeof(body));
	CK(body[0x00] == 0 && body[0x03] == 0, "reserved bytes not zero");
	CK(body[0x01] == 5, "ssid_len byte %u", body[0x01]);
	CK(body[0x02] == 2, "mfp byte %u", body[0x02]);
	CK(body[0x10] == 1, "privacy byte %u", body[0x10]);
	CK(body[0x11] == 3, "auth byte %u", body[0x11]);
	CK(body[0x12] == 5, "key_len byte %u", body[0x12]);
	CK(body[0x13] == 1, "key_idx byte %u", body[0x13]);
	CK((u16)(body[0x04] | (body[0x05] << 8)) == (u16)b.ie_va,
	   "ie address low bytes");
	CK((u32)(body[0x68] | (body[0x69] << 8) | (body[0x6a] << 16) |
		 ((u32)body[0x6b] << 24)) == 8, "ie_len dword");
	CK(hi5622_sta_connect_body_check(body, sizeof(body), &b) == 0,
	   "built body fails its own check");
	body[0x03] = 1;
	CK(hi5622_sta_connect_body_check(body, sizeof(body), &b) == -EINVAL,
	   "reserved-byte mutation accepted");
	rc = hi5622_sta_connect_body(body, sizeof(body), &b);
	CK(rc == 0, "rebuild failed");

	/* zero address is not a legal 'absent' field */
	b.key_va = 0;
	CK(hi5622_sta_connect_body(body, sizeof(body), &b) == -EINVAL,
	   "zero key address accepted by builder");
	fill_bufs(&b, buf);

	rc = hi5622_sta_wal_frame(frame, sizeof(frame), &flen,
				  HI5622_STA_WAL_CMD_CONNECT, body,
				  sizeof(body), 7u, 0u);
	CK(rc == 0, "WAL frame build failed: %d", rc);
	CK(flen == 140, "connect frame len %zu", flen);
	CK((frame[0] & 0x0fu) == 2, "group nibble %u", frame[0]);
	CK(frame[1] == 0x01, "alloc/dst byte 0x%02x", frame[1]);
	CK((u16)(frame[4] | (frame[5] << 8)) == 140, "total len");
	CK((u16)(frame[6] | (frame[7] << 8)) == 0, "HCC id");
	CK((u16)(frame[0x0a] | (frame[0x0b] << 8)) == 0x5a5a, "tag");
	CK((u32)(frame[0x10] | (frame[0x11] << 8) | (frame[0x12] << 16) |
		 ((u32)frame[0x13] << 24)) == 1, "WAL message id");
	CK((u16)(frame[0x16] | (frame[0x17] << 8)) == 112, "WAL data_len");
	CK((u32)(frame[0x18] | (frame[0x19] << 8) | (frame[0x1a] << 16) |
		 ((u32)frame[0x1b] << 24)) == 7, "WAL sequence");
	CK((u16)(frame[0x1c] | (frame[0x1d] << 8)) == 0x123, "WAL opcode");
	CK((u16)(frame[0x1e] | (frame[0x1f] << 8)) == 108, "WAL body_len");
	CK(memcmp(frame + 32, body, 108) == 0, "body not at +0x20");
	CK(hi5622_sta_frame_check(frame, flen, HI5622_STA_WAL_CMD_CONNECT,
				  108) == 0, "frame self-check failed");
	frame[0x1c] = 0xd0;
	CK(hi5622_sta_frame_check(frame, flen, HI5622_STA_WAL_CMD_CONNECT,
				  108) == -EINVAL, "mutated opcode accepted");

	memset(&dp, 0, sizeof(dp));
	memcpy(dp.bssid, test_bssid, 6);
	dp.reason_code = 3;
	rc = hi5622_sta_disconnect_body(body, sizeof(body), &dp);
	CK(rc == 0, "disconnect body build failed");
	CK(body[0x0f] == 1, "disconnect magic byte");
	CK((u16)(body[6] | (body[7] << 8)) == 3, "disconnect reason");
	CK(memcmp(body, test_bssid, 6) == 0, "disconnect bssid");
	rc = hi5622_sta_wal_frame(frame, sizeof(frame), &flen,
				  HI5622_STA_WAL_CMD_DISCONNECT, body, 16u,
				  9u, 0u);
	CK(rc == 0 && flen == 48, "disconnect frame build/len");
	CK(hi5622_sta_frame_check(frame, flen,
				  HI5622_STA_WAL_CMD_DISCONNECT, 16) == 0,
	   "disconnect frame self-check");
}

/* ---------------- T6: D2H completion decode ---------------- */
static void t6_d2h(void)
{
	u8 hdr[HI5622_STA_D2H_HDR_LEN];
	u8 p[HI5622_STA_ASSOC_MIN_LEN];
	struct hi5622_sta_assoc_comp ac;
	struct hi5622_sta_disasoc_comp dc;
	const u32 buf = 0x81234000u;
	int rc;

	memset(p, 0, sizeof(p));
	put16(p + HI5622_STA_ASSOC_OFF_STATUS, 0);
	put16(p + HI5622_STA_ASSOC_OFF_UNK0, 0x1234);
	put16(p + HI5622_STA_ASSOC_OFF_UNKA, 0x5678);
	memcpy(p + HI5622_STA_ASSOC_OFF_BSSID, test_bssid, 6);
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_LEN, 4);
	put32(p + HI5622_STA_ASSOC_OFF_RESP_IE_LEN, 8);
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_VA, buf + 0x100);
	put32(p + HI5622_STA_ASSOC_OFF_RESP_IE_VA, buf + 0x200);
	d2h_hdr(hdr, HI5622_STA_D2H_ID_ASSOC_COMP, sizeof(p));
	rc = hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					  DMA_LO, DMA_HI, &ac);
	CK(rc == 0, "valid assoc completion rejected: %d", rc);
	if (rc == 0) {
		CK(ac.status == 0, "status %u", ac.status);
		CK(memcmp(ac.bssid, test_bssid, 6) == 0, "bssid");
		CK(ac.req_ie_len == 4 && ac.resp_ie_len == 8, "ie lens");
		CK(ac.req_ie_va == buf + 0x100, "req ie address");
		CK(ac.resp_ie_va == buf + 0x200, "resp ie address");
		CK(ac.unknown0 == 0x1234 && ac.unknown_a == 0x5678,
		   "unknown fields");
	}
	/* short payload */
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p,
					HI5622_STA_ASSOC_MIN_LEN - 1,
					DMA_LO, DMA_HI, &ac) == -ENOSPC,
	   "short payload accepted");
	/* wrong group / id / tag / total */
	d2h_hdr(hdr, HI5622_STA_D2H_ID_ASSOC_COMP, sizeof(p));
	hdr[0] = 3;
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -EINVAL,
	   "wrong group accepted");
	d2h_hdr(hdr, HI5622_STA_D2H_ID_DISASOC_COMP, sizeof(p));
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -EINVAL,
	   "wrong id accepted");
	d2h_hdr(hdr, HI5622_STA_D2H_ID_ASSOC_COMP, sizeof(p));
	hdr[0x0a] = 0;
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -EINVAL,
	   "wrong tag accepted");
	d2h_hdr(hdr, HI5622_STA_D2H_ID_ASSOC_COMP, sizeof(p));
	hdr[0x04] = 0;
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -EINVAL,
	   "wrong total accepted");
	/* untrusted IE addresses / lengths */
	d2h_hdr(hdr, HI5622_STA_D2H_ID_ASSOC_COMP, sizeof(p));
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_LEN, 1025);
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -E2BIG,
	   "oversized req ie accepted");
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_LEN, 4);
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_VA, 0x40000000u);
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -ERANGE,
	   "out-of-window req ie accepted");
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_VA, 0x81234001u);
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -ERANGE,
	   "misaligned req ie accepted");
	/* region end past the window end = untrusted length/address pair */
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_LEN, 1024);
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_VA, 0x8ffffffcu);
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -ERANGE,
	   "over-window req ie region accepted");
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_LEN, 4);
	put32(p + HI5622_STA_ASSOC_OFF_REQ_IE_VA, buf + 0x100);
	put32(p + HI5622_STA_ASSOC_OFF_RESP_IE_VA, 0x8fffffffu);
	CK(hi5622_sta_assoc_comp_decode(hdr, sizeof(hdr), p, sizeof(p),
					DMA_LO, DMA_HI, &ac) == -ERANGE,
	   "out-of-window resp ie accepted");

	/* disassoc completion */
	put16(p, 3);
	d2h_hdr(hdr, HI5622_STA_D2H_ID_DISASOC_COMP, 2);
	rc = hi5622_sta_disasoc_comp_decode(hdr, sizeof(hdr), p, 2, &dc);
	CK(rc == 0 && dc.reason == 3, "disassoc completion decode");
	CK(hi5622_sta_disasoc_comp_decode(hdr, sizeof(hdr), p, 1, &dc)
	   == -ENOSPC, "short disassoc payload accepted");
}

/* ---------------- T7: decoded capability constants ---------------- */
static void t7_constants(void)
{
	u16 n = 0;
	const struct hi5622_sta_chan *ch;

	CK(hi5622_sta_ht_cap_word(HI5622_STA_HT_MODE_VENDOR40) == 0x0062,
	   "vendor HT40 word");
	CK(hi5622_sta_ht_mode_is_40(HI5622_STA_HT_MODE_VENDOR40) == 1,
	   "HT40 mode flag");
	CK(hi5622_sta_vht_cap_word(1) == 0x00400064u, "5G VHT word");
	CK(hi5622_sta_vht_cap_word(0) == 0x00400000u, "2G VHT word");
	CK(hi5622_sta_vht_cap_word(2) == 0xffffffffu, "bad band VHT word");
	ch = hi5622_sta_band_channels(0, &n);
	CK(ch && n == 14, "2.4G channel count %u", n);
	ch = hi5622_sta_band_channels(1, &n);
	CK(ch && n == 29, "5G channel count %u", n);
	CK(hi5622_sta_auth_type_valid(8) == 1 &&
	   hi5622_sta_auth_type_valid(9) == 0, "auth type range");
}

int main(void)
{
	printf("wifi-sta-join host selftest\n");
	t1_request_bounds();
	t2_gates();
	t3_addresses();
	t4_connect_frame();
	t6_d2h();
	t7_constants();
	printf("checks=%d fails=%d\n", checks, fails);
	if (fails) {
		printf("STA-JOIN-SELFTEST-FAIL\n");
		return 1;
	}
	printf("STA-JOIN-SELFTEST-PASS\n");
	return 0;
}
