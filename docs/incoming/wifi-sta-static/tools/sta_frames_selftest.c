/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sta_frames_selftest.c - host-side assertions for the STA WAL frame
 * builders (wifi-sta-static lane, host-only; no device, no vendor binary).
 *
 * Build/run: ./tools/selftest.sh
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../src/hi5622_sta_frames.h"

static int fails;

#define CHECK(cond, msg) do {						\
	if (!(cond)) {							\
		printf("FAIL: %s\n", msg);				\
		fails++;						\
	}								\
} while (0)

static u16 get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static void test_connect_body(void)
{
	u8 body[HI5622_STA_CONNECT_BODY_LEN];
	struct hi5622_sta_join_bufs p;
	int rc;

	memset(&p, 0, sizeof(p));
	p.ssid_len = 9;
	p.mfp = 1;
	p.privacy = 1;
	p.auth_type = 4;	/* SAE */
	p.key_len = 16;
	p.key_idx = 2;
	p.ie_len = 40;
	p.ssid_va = 0x80010000u;
	p.bssid_va = 0x80010008u;
	p.ie_va = 0x80010048u;
	p.key_va = 0x80010100u;

	rc = hi5622_sta_connect_body(body, sizeof(body), &p);
	CHECK(rc == 0, "connect body builds");
	CHECK(body[0x01] == 9, "body[1] = ssid_len");
	CHECK(body[0x02] == 1, "body[2] = mfp");
	CHECK(get32(body + 0x04) == 0x80010048u, "body[4] = ie address");
	CHECK(get32(body + 0x08) == 0x80010000u, "body[8] = ssid address");
	CHECK(get32(body + 0x0c) == 0x80010008u, "body[12] = bssid address");
	CHECK(body[0x10] == 1, "body[16] = privacy");
	CHECK(body[0x11] == 4, "body[17] = auth type");
	CHECK(body[0x12] == 16, "body[18] = key len");
	CHECK(body[0x13] == 2, "body[19] = key idx");
	CHECK(get32(body + 0x14) == 0x80010100u, "body[20] = key address");
	CHECK(get32(body + 0x68) == 40u, "body[104] = ie len");
	CHECK(body[0x00] == 0 && body[0x03] == 0, "reserved bytes zero");

	/* rejection paths */
	p.ssid_len = 0;
	CHECK(hi5622_sta_connect_body(body, sizeof(body), &p) == -EINVAL,
	      "ssid_len 0 refused");
	p.ssid_len = HI5622_STA_MAX_SSID + 1;
	CHECK(hi5622_sta_connect_body(body, sizeof(body), &p) == -EINVAL,
	      "ssid_len > 32 refused");
	p.ssid_len = 8;
	p.auth_type = 9;
	CHECK(hi5622_sta_connect_body(body, sizeof(body), &p) == -EINVAL,
	      "auth type 9 refused");
	p.auth_type = 0;
	p.ie_len = HI5622_STA_MAX_IE + 1;
	CHECK(hi5622_sta_connect_body(body, sizeof(body), &p) == -EINVAL,
	      "ie_len > 512 refused");
	p.ie_len = 0;
	p.key_len = HI5622_STA_MAX_KEY + 1;
	CHECK(hi5622_sta_connect_body(body, sizeof(body), &p) == -EINVAL,
	      "key_len > 32 refused");
	CHECK(hi5622_sta_connect_body(body, 8, &p) == -ENOSPC,
	      "short body buffer refused");
}

static void test_disconnect_body(void)
{
	u8 body[HI5622_STA_DISCONNECT_BODY_LEN];
	struct hi5622_sta_disconnect_params p;

	memset(&p, 0, sizeof(p));
	p.bssid[0] = 0x02; p.bssid[5] = 0x42;
	p.reason_code = 3;
	CHECK(hi5622_sta_disconnect_body(body, sizeof(body), &p) == 0,
	      "disconnect body builds");
	CHECK(memcmp(body, p.bssid, 6) == 0, "disconnect bssid");
	CHECK(get16(body + 6) == 3, "disconnect reason");
	CHECK(body[15] == 1, "disconnect body[15] = 1 (vendor constant)");
}

static void test_wal_frame(void)
{
	u8 body[HI5622_STA_CONNECT_BODY_LEN];
	u8 frame[HI5622_STA_FRAME_MAX];
	u8 bad[HI5622_STA_FRAME_MAX];
	struct hi5622_sta_join_bufs p;
	size_t len = 0;
	int rc;

	memset(&p, 0, sizeof(p));
	p.ssid_len = 5;
	CHECK(hi5622_sta_connect_body(body, sizeof(body), &p) == 0, "body");

	rc = hi5622_sta_wal_frame(frame, sizeof(frame), &len,
				  HI5622_STA_WAL_CMD_CONNECT, body,
				  sizeof(body), 7u, 0u);
	CHECK(rc == 0, "connect frame builds");
	CHECK(len == HI5622_STA_WAL_HDR_LEN + sizeof(body), "frame total 140");
	CHECK(len == 140u, "frame total is 140 bytes");
	CHECK((frame[0] & 0x0f) == HI5622_STA_HCC_GROUP_CORE, "hcc group 2");
	CHECK(get16(frame + 4) == 140, "hcc total field");
	CHECK(get16(frame + 6) == HI5622_STA_HCC_ID_WAL, "hcc id 0 (WAL carrier)");
	CHECK(get16(frame + 0x0a) == HI5622_STA_HCC_TAG, "hcc tag 0x5a5a");
	CHECK(get32(frame + 0x10) == HI5622_STA_WAL_MSG_CFG, "wal cfg id 1");
	CHECK(get16(frame + 0x16) == 4 + 108, "wal data_len 112");
	CHECK(get32(frame + 0x18) == 7u, "wal seq");
	CHECK(get16(frame + 0x1c) == HI5622_STA_WAL_CMD_CONNECT,
	      "wal opcode 0x123");
	CHECK(get16(frame + 0x1e) == 108, "wal body_len 108");
	CHECK(memcmp(frame + HI5622_STA_WAL_HDR_LEN, body, sizeof(body)) == 0,
	      "body copied at +0x20");
	CHECK(hi5622_sta_frame_check(frame, len, HI5622_STA_WAL_CMD_CONNECT,
				     sizeof(body)) == 0, "frame self-check");

	/* mutation tests: every single-field corruption must be refused */
	memcpy(bad, frame, len); bad[0] = 0x00;	/* group 0 */
	CHECK(hi5622_sta_frame_check(bad, len, 0x123, 108) != 0,
	      "wrong group refused");
	memcpy(bad, frame, len); bad[0x0a] ^= 0xff;
	CHECK(hi5622_sta_frame_check(bad, len, 0x123, 108) != 0,
	      "wrong tag refused");
	memcpy(bad, frame, len); bad[0x10] = 2;
	CHECK(hi5622_sta_frame_check(bad, len, 0x123, 108) != 0,
	      "wrong wal id refused");
	memcpy(bad, frame, len); bad[0x1c] = 0xd0;
	CHECK(hi5622_sta_frame_check(bad, len, 0x123, 108) != 0,
	      "wrong opcode refused");
	memcpy(bad, frame, len); bad[0x16] = 4;
	CHECK(hi5622_sta_frame_check(bad, len, 0x123, 108) != 0,
	      "wrong data_len refused");
	CHECK(hi5622_sta_frame_check(frame, len - 1, 0x123, 108) != 0,
	      "truncated frame refused");

	/* disconnect frame (opcode already in the inherited WAL allowlist) */
	{
		u8 dbody[HI5622_STA_DISCONNECT_BODY_LEN];
		struct hi5622_sta_disconnect_params dp;

		memset(&dp, 0, sizeof(dp));
		CHECK(hi5622_sta_disconnect_body(dbody, sizeof(dbody), &dp) == 0,
		      "disconnect body");
		rc = hi5622_sta_wal_frame(frame, sizeof(frame), &len,
					  HI5622_STA_WAL_CMD_DISCONNECT, dbody,
					  sizeof(dbody), 8u, 0u);
		CHECK(rc == 0 && len == 48u, "disconnect frame 48 bytes");
		CHECK(get16(frame + 0x1c) == 0x00d0u, "disconnect opcode 0xd0");
		CHECK(hi5622_sta_frame_check(frame, len, 0x00d0u, 16u) == 0,
		      "disconnect self-check");
	}

	/* builder rejection paths */
	CHECK(hi5622_sta_wal_frame(frame, 8, &len, 0x123, body, 108, 1, 0)
	      == -ENOSPC, "short frame buffer refused");
	CHECK(hi5622_sta_wal_frame(frame, sizeof(frame), &len, 0, body, 108, 1, 0)
	      == -EINVAL, "zero opcode refused");
	CHECK(hi5622_sta_wal_frame(frame, sizeof(frame), &len, 0x123, body, 108, 1, 16)
	      == -EINVAL, "dst > 15 refused");
	CHECK(hi5622_sta_wal_frame(frame, sizeof(frame), &len, 0x123, NULL, 108, 1, 0)
	      == -EINVAL, "null body with nonzero len refused");
}

int main(void)
{
	test_connect_body();
	test_disconnect_body();
	test_wal_frame();
	if (fails) {
		printf("sta_frames_selftest: FAIL %d check(s)\n", fails);
		return 1;
	}
	printf("sta_frames_selftest: PASS (connect body 108 B, disconnect body 16 B, WAL frame 140/48 B, mutation rejection)\n");
	return 0;
}
