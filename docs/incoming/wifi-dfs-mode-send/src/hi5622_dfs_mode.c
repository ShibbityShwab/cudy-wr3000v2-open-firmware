// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_dfs_mode.c - H2D 0x01F7 RF/DFS mode frame builder and the
 * fail-closed AP-start gate (see the header for the decoded contract).
 *
 * ORIGINAL clean-room code (lane wifi-dfs-mode-send, task-190).  The frame
 * layout is the committed g0-id1 config-event shape of
 * ../wifi-fw-desc-table/TABLES.md T2; the key/worker/mode facts are
 * ../wifi-fw-rf-descriptor/TABLES.md (table A row 93, CA 0xB5B9C, mode 3).
 * This file only synthesizes the host-built protocol bytes and refuses
 * malformed requests.
 */
#include "hi5622_dfs_mode.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static void dm_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

int hi5622_dfs_mode_plan(unsigned int dfs_mode, unsigned int beacon_static,
			 unsigned int band, struct hi5622_dfs_mode_plan *out)
{
	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (dfs_mode > HI5622_DFS_MODE_PARM_MAX)
		return -EINVAL;
	if (band > 1u)
		return -EINVAL;
	/* An explicit 0x01F7 request without the vendor-order AP start cannot be
	 * honoured (the frame has no safe slot in the legacy ladder): refuse
	 * instead of silently dropping it. */
	if (dfs_mode == HI5622_DFS_MODE_PARM_MAX && beacon_static != 1u)
		return -EINVAL;
	out->mode = (u8)HI5622_DFS_MODE_MODE;
	/* Band-1 AP start only: the mode byte this event writes is consumed by
	 * the band-1-only RF programme CA 0xA2FA2. */
	out->post = (dfs_mode == HI5622_DFS_MODE_PARM_MAX && band == 1u);
	return 0;
}

int hi5622_dfs_mode_build(u8 *wire, size_t cap, size_t *out_len,
			  const struct hi5622_dfs_mode_plan *plan)
{
	u8 *b;

	if (!wire || !out_len || !plan)
		return -EINVAL;
	if (!plan->post)
		return -EINVAL;		/* never build an unrequested frame */
	if (plan->mode != (u8)HI5622_DFS_MODE_MODE)
		return -EINVAL;
	if (cap < HI5622_DFS_MODE_WIRE_LEN)
		return -ENOSPC;

	b = wire;
	memset(b, 0, HI5622_DFS_MODE_WIRE_LEN);

	/* 12-byte HCC header: group 0, id 1 (the config-event carrier), the
	 * alloc/dst byte, the inherited fa/fb values, total = wire length,
	 * message id 1 at +6, fc/dst 0, transport tag 0x5A5A at +10.  These
	 * are the same bytes as the sibling 0x00F7 frame (the vendor builds
	 * both through hmac_config_send_event's shared header path). */
	b[0] = 0x00u;				/* group 0 */
	b[1] = 0x01u;				/* alloc nibble 1, dst 0 */
	b[2] = (u8)HI5622_DFS_MODE_FA;
	b[3] = (u8)HI5622_DFS_MODE_FB;
	dm_put16(b + 4, (u16)HI5622_DFS_MODE_WIRE_LEN);	/* total 0x001D */
	dm_put16(b + 6, (u16)HI5622_DFS_MODE_ID);
	b[8] = 0x00u;				/* fc */
	b[9] = 0x00u;				/* dst */
	dm_put16(b + 10, (u16)HI5622_DFS_MODE_TAG);

	/* 8-byte token, then key u16 at hdr+20 and body length at hdr+22 (the
	 * firmware g0-id1 handler reads key = u16[msg+20] and passes
	 * body = msg+24, len = u16[msg+22] to the worker).  The token is the
	 * inherited bench-executed g0-id1 pattern (payload word at +4 = 1). */
	b[HI5622_DFS_MODE_TOKEN_OFF + 4u] = 0x01u;
	dm_put16(b + HI5622_DFS_MODE_KEY_OFF, (u16)HI5622_DFS_MODE_KEY);
	dm_put16(b + HI5622_DFS_MODE_LEN_OFF, (u16)HI5622_DFS_MODE_BODY_LEN);

	/* body {0x03}: mode 3, the value the ch36..64 RF programme requires
	 * (CA 0xB5BE2 reads body[0]; CA 0xA2FA2 compares the mode byte to 3). */
	b[HI5622_DFS_MODE_BODY_OFF] = plan->mode;

	/* The inherited 4-byte 0xFF tail every working g0-id1 frame carries. */
	memset(b + HI5622_DFS_MODE_TAIL_OFF, 0xff, 4);

	*out_len = HI5622_DFS_MODE_WIRE_LEN;
	return 0;
}

int hi5622_dfs_mode_precheck(u32 published_perlink, u8 attach_flag)
{
	/* The 0x01F7 worker CA 0xB5B9C loads the published per-link
	 * (bandobj+0x1A00) and logs 1442 when it is zero, then tests the band
	 * attach flag (bandobj+0xD70) and logs 1448 when it is not 1.  Both
	 * are read-only verdicts; the caller logs them and continues. */
	if (!published_perlink || attach_flag != 1u)
		return -EAGAIN;
	return 0;
}

int hi5622_dfs_mode_status_line(unsigned int dfs_mode,
				unsigned int beacon_static, int last_rc,
				unsigned int posted, unsigned int refused,
				char *buf, size_t len)
{
	const char *mode;
	int n;

	if (!buf || !len)
		return -EINVAL;
	if (dfs_mode == 0u)
		mode = "off";
	else if (dfs_mode == HI5622_DFS_MODE_PARM_MAX)
		mode = "band1-post";
	else
		mode = "invalid";
	n = snprintf(buf, len,
		     "dfs_mode: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u\n",
		     dfs_mode, mode, beacon_static, last_rc, posted, refused);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
