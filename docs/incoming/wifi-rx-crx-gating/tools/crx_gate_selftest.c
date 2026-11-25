/* SPDX-License-Identifier: GPL-2.0 */
/*
 * crx_gate_selftest.c - host selftest for the CRX delivery contract
 * (lane wifi-rx-crx-gating, shared task task-164).
 *
 * Strict C99, no kernel, no device, no vendor bytes: every check pins a
 * derived fact from REPORT.md (the firmware builder at CPU 0xBA144 and the
 * host dispatch table at .data+0x584) plus the fail-closed behaviour of
 * hcg_decode_crx() / hcg_classify().
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "hi5622_crx_gate.h"

static int failures;
static int checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHK_ERR(expr, want) do { \
	int rc_ = (expr); \
	checks++; \
	if (rc_ != (want)) { \
		failures++; \
		printf("FAIL %s:%d: rc=%d want=%d for %s\n", \
		       __FILE__, __LINE__, rc_, (want), #expr); \
	} \
} while (0)

/* Build the exact wire form the firmware builder produces (REPORT section 2):
 * 12-byte HCC header + 48-byte RX info + frame_len-byte 802.11 MPDU. */
static unsigned int build_crx(unsigned char *buf, unsigned int cap,
			      unsigned int frame_len)
{
	unsigned int total = HCG_PREFIX_LEN + frame_len;
	unsigned char *info = buf + HCG_HCC_HDR_LEN;
	unsigned char *frame = buf + HCG_PREFIX_LEN;
	unsigned int i;

	if (total > cap)
		return 0;
	memset(buf, 0, total);
	buf[0] = HCG_GROUP_HMAC;
	buf[1] = 0x21;                      /* dst nibble 2, bid 1 */
	buf[4] = (unsigned char)(total & 0xff);
	buf[5] = (unsigned char)(total >> 8);
	buf[6] = HCG_ID_CRX;
	buf[10] = 0x5a;
	buf[11] = 0x5a;
	info[HCG_INFO_CHANNEL] = 6;
	info[HCG_INFO_FRAME_LEN] = (unsigned char)(frame_len & 0xff);
	info[HCG_INFO_FRAME_LEN + 1] = (unsigned char)(frame_len >> 8);
	info[HCG_INFO_RSSI] = (unsigned char)(unsigned char)-42;
	info[HCG_INFO_FLAG] = 0x03;
	info[HCG_INFO_HDRLEN] = 24;
	frame[0] = 0xb0;                    /* management, subtype auth */
	frame[1] = 0x00;
	for (i = 2; i < frame_len; i++)
		frame[i] = (unsigned char)i;
	return total;
}

static void test_constants(void)
{
	CHECK(HCG_PREFIX_LEN == 60);
	CHECK(HCG_HCC_HDR_LEN == 12);
	CHECK(HCG_RX_INFO_LEN == 48);
	CHECK(HCG_FW_TOTAL_CAP == 0x680);
	CHECK(HCG_MAX_FRAME_LEN == 0x644);
	CHECK(HCG_GROUP_HMAC == 1);
	CHECK(HCG_ID_CRX == 13);
	CHECK(HCG_INFO_CHANNEL == 7);
	CHECK(HCG_INFO_FRAME_LEN == 17);
	CHECK(HCG_INFO_RSSI == 39);
	CHECK(HCG_FW_DR_STRIDE == 0x354);
	CHECK(HCG_FW_DR_COUNT == 4);
}

static void test_decode_ok(void)
{
	unsigned char buf[HCG_FW_TOTAL_CAP];
	struct hcg_decode d;
	unsigned int total;

	memset(&d, 0, sizeof(d));
	total = build_crx(buf, sizeof(buf), 24);
	CHECK(total == 84);
	CHK_ERR(hcg_decode_crx(total, buf, &d), 0);
	CHECK(d.channel == 6);
	CHECK(d.frame_len == 24);
	CHECK(d.rssi == -42);
	CHECK(d.dst_nibble == 2);
	CHECK(d.frame == buf + HCG_PREFIX_LEN);
	CHECK(d.frame[0] == 0xb0);

	/* Largest accepted frame: total 0x680 -> frame_len 0x644. */
	total = build_crx(buf, sizeof(buf), HCG_MAX_FRAME_LEN);
	CHECK(total == HCG_FW_TOTAL_CAP);
	CHK_ERR(hcg_decode_crx(total, buf, &d), 0);
	CHECK(d.frame_len == HCG_MAX_FRAME_LEN);

	/* Minimum accepted frame. */
	total = build_crx(buf, sizeof(buf), HCG_MIN_FRAME_LEN);
	CHECK(total == HCG_PREFIX_LEN + HCG_MIN_FRAME_LEN);
	CHK_ERR(hcg_decode_crx(total, buf, &d), 0);
}

static void test_decode_refusals(void)
{
	unsigned char buf[HCG_FW_TOTAL_CAP + 8];
	struct hcg_decode d;
	unsigned int total;
	unsigned int full;

	total = build_crx(buf, sizeof(buf), 24);
	full = total;

	/* NULL arguments. */
	CHK_ERR(hcg_decode_crx(full, NULL, &d), -EINVAL);
	CHK_ERR(hcg_decode_crx(full, buf, NULL), -EINVAL);
	/* Short total. */
	CHK_ERR(hcg_decode_crx(HCG_PREFIX_LEN + HCG_MIN_FRAME_LEN - 1, buf, &d),
		-ENODATA);
	/* Over the firmware cap. */
	CHK_ERR(hcg_decode_crx(HCG_FW_TOTAL_CAP + 1, buf, &d), -EMSGSIZE);
	/* Bad tag. */
	buf[10] = 0x00;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[10] = 0x5a;
	/* Wrong group. */
	buf[0] = 4;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[0] = HCG_GROUP_HMAC;
	/* Wrong id. */
	buf[6] = 14;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[6] = HCG_ID_CRX;
	/* Header total disagrees with the length argument. */
	buf[5] = 0x01;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[5] = 0x00;
	/* RX-info frame length disagrees with total-60. */
	buf[HCG_HCC_HDR_LEN + HCG_INFO_FRAME_LEN] = 23;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[HCG_HCC_HDR_LEN + HCG_INFO_FRAME_LEN] = 24;
	/* Forward flag clear (firmware would have dropped it). */
	buf[HCG_HCC_HDR_LEN + HCG_INFO_FLAG] = 0;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -ENODATA);
	buf[HCG_HCC_HDR_LEN + HCG_INFO_FLAG] = 1;
	/* Channel outside the band plans. */
	buf[HCG_HCC_HDR_LEN + HCG_INFO_CHANNEL] = 0;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -ERANGE);
	buf[HCG_HCC_HDR_LEN + HCG_INFO_CHANNEL] = 20;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -ERANGE);
	buf[HCG_HCC_HDR_LEN + HCG_INFO_CHANNEL] = 6;
	/* RSSI out of the physical range. */
	buf[HCG_HCC_HDR_LEN + HCG_INFO_RSSI] = 1;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -ERANGE);
	buf[HCG_HCC_HDR_LEN + HCG_INFO_RSSI] = (unsigned char)-121;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -ERANGE);
	buf[HCG_HCC_HDR_LEN + HCG_INFO_RSSI] = (unsigned char)-42;
	/* Not a management frame (data type). */
	buf[HCG_PREFIX_LEN] = 0x08;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[HCG_PREFIX_LEN] = 0xb0;
	/* Wrong CF version. */
	buf[HCG_PREFIX_LEN] = 0xb1;
	CHK_ERR(hcg_decode_crx(full, buf, &d), -EPROTO);
	buf[HCG_PREFIX_LEN] = 0xb0;
	/* Still valid after restoring. */
	CHK_ERR(hcg_decode_crx(full, buf, &d), 0);
}

static void probe_clean(struct hcg_probe *p)
{
	memset(p, 0, sizeof(*p));
	p->mac_ok = 1;
	p->fw_ok = 1;
	p->mac_mask = HCG_MAC_MASK_EXPECTED;
	p->mac_norm_rptr = HCG_MAC_PREFETCH_EXPECTED;
	p->mac_small_rptr = HCG_MAC_PREFETCH_EXPECTED;
	p->mac_cmp_wptr = 0;
	p->mac_status = 0x00000182;
	p->fw_hcc_instance = HCG_FW_HCC_INSTANCE_CA;
	p->fw_dr_base = 0x0010F2B8;
}

static void test_classify(void)
{
	struct hcg_probe p;

	/* NULL / unreadable halves. */
	CHECK(hcg_classify(NULL) == HCG_V_REFUSED);
	probe_clean(&p);
	p.mac_ok = 0;
	CHECK(hcg_classify(&p) == HCG_V_MAC_UNREADABLE);
	probe_clean(&p);
	p.fw_ok = 0;
	CHECK(hcg_classify(&p) == HCG_V_FW_UNREADABLE);

	/* The measured bench state: D2H alive, MAC RX idle -> upstream. */
	probe_clean(&p);
	p.d2h_frames_seen = 2;
	p.fw_dr_producer[3] = 0x0000080c;
	p.fw_dr_trigger[3] = 7;
	CHECK(hcg_classify(&p) == HCG_V_MAC_RX_IDLE);

	/* Same, but with a CRX seen -> delivered. */
	p.crx_seen = 1;
	p.crx_bytes = 84;
	CHECK(hcg_classify(&p) == HCG_V_CRX_ONLY);
	/* CRX plus host-ring activity. */
	p.mac_cmp_wptr = 0x00000001;
	CHECK(hcg_classify(&p) == HCG_V_CRX_AND_DATA);

	/* RX status bit 2 set is host-ring activity too. */
	p.mac_cmp_wptr = 0;
	p.mac_status = 0x00000186;
	CHECK(hcg_classify(&p) == HCG_V_CRX_AND_DATA);

	/* Nothing ever deposited and no catalogue -> firmware machinery idle. */
	probe_clean(&p);
	CHECK(hcg_classify(&p) == HCG_V_FW_MACHINERY_IDLE);

	/* D2H alive but unexpected ring shape -> inconsistent. */
	probe_clean(&p);
	p.d2h_frames_seen = 3;
	p.fw_dr_producer[0] = 1;
	p.mac_cmp_wptr = 5;
	CHECK(hcg_classify(&p) == HCG_V_INCONSISTENT);

	/* Prefetch only one ring (not both) is not the measured state. */
	probe_clean(&p);
	p.d2h_frames_seen = 3;
	p.fw_dr_producer[0] = 1;
	p.mac_small_rptr = 0;
	CHECK(hcg_classify(&p) == HCG_V_INCONSISTENT);
}

static void test_names(void)
{
	CHECK(strcmp(hcg_verdict_name(HCG_V_MAC_RX_IDLE),
		     "MAC-RX-IDLE-UPSTREAM") == 0);
	CHECK(strcmp(hcg_verdict_name(HCG_V_CRX_ONLY),
		     "CRX-DELIVERED") == 0);
	CHECK(strstr(hcg_verdict_reason(HCG_V_MAC_RX_IDLE),
		     "upstream") != NULL);
	CHECK(hcg_verdict_name(HCG_V_REFUSED) != NULL);
	CHECK(hcg_verdict_name(HCG_V_MAC_UNREADABLE) != NULL);
	CHECK(hcg_verdict_name(HCG_V_FW_UNREADABLE) != NULL);
	CHECK(hcg_verdict_name(HCG_V_FW_MACHINERY_IDLE) != NULL);
	CHECK(hcg_verdict_name(HCG_V_CRX_AND_DATA) != NULL);
	CHECK(hcg_verdict_name(HCG_V_INCONSISTENT) != NULL);
}

/* The firmware builder contract arithmetic cross-check: the same numbers
 * the host decoder enforces must reproduce the builder's own cap logic. */
static void test_builder_contract(void)
{
	unsigned int min_total = HCG_MIN_FRAME_LEN + HCG_PREFIX_LEN;
	unsigned int max_total = HCG_MAX_FRAME_LEN + HCG_PREFIX_LEN;

	/* The firmware builder's own arithmetic (0xBA0F8/0xBA146): total =
	 * frame_len + 60, accepted only while total <= 0x680 (0xBA110). */
	CHECK(min_total == 70);
	CHECK(max_total == HCG_FW_TOTAL_CAP);
	CHECK(HCG_MAX_FRAME_LEN + HCG_PREFIX_LEN == HCG_FW_TOTAL_CAP);
	CHECK(HCG_MAX_FRAME_LEN + HCG_PREFIX_LEN + 1 > HCG_FW_TOTAL_CAP);
	CHECK(HCG_FW_TOTAL_CAP - HCG_PREFIX_LEN == HCG_MAX_FRAME_LEN);
}

int main(void)
{
	test_constants();
	test_decode_ok();
	test_decode_refusals();
	test_classify();
	test_names();
	test_builder_contract();
	printf("crx_gate_selftest: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
