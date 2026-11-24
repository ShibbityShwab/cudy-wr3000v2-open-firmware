// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta.c - cfg80211 STA connect/disconnect for the Hi5622V100
 * clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141,
 * 2026-11-24).  No vendor source text/object/firmware byte reproduced.
 * Decoded facts and their evidence markers: ../REPORT.md sections 3-5.
 *
 * WHAT THE VENDOR DOES (2.5.24, link-time .text offsets)
 * -----------------------------------------------------
 *   wal_cfg80211_connect            +0x100590
 *     -> wal_cfg80211_start_connect +0x128310
 *        -> wal_cfg80211_start_req  +0x127ec4
 *           -> wal_send_cfg_event   +0x1270d8
 *              == HCC group 2, HCC id 0, WAL cfg-event id 1, WAL opcode
 *                 0x0123, body 108 B (hi5622_sta_frames.h)
 *   wal_cfg80211_disconnect         +0x100844
 *     -> wal_cfg80211_start_disconnect +0x128334
 *        -> WAL opcode 0x00d0, body 16 B {bssid[6], reason u16, [15]=1}
 *
 * The 108-byte connect body carries four host-DRAM buffer addresses
 * (bssid/ssid/ie/key).  The device reaches host DRAM through the same
 * inbound window the data-plane rings use (devva == dma_addr), which makes
 * the vendor-shaped frame expressible here - but the exact firmware
 * dereference contract is [U], so posting is gated twice:
 *
 *   hi5622_sta_join=0  (default) fail closed: -EOPNOTSUPP, no frame built
 *   hi5622_sta_join=1            build + validate + log the frame, no post
 *   hi5622_sta_join=2            post it (EXPERIMENTAL: needs allow_frames>0
 *                                and the bench ladder in TEST-LADDER.md)
 *
 * cfg80211_connect_done() is deliberately NOT called: the firmware's connect
 * result arrives on the D2H management path, which this driver does not
 * decode yet (the RX lane owns it).  Returning success without completing
 * would be a fake success; userspace sees a clean timeout instead.
 */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_sta.h"
#include "hi5622_sta_frames.h"
#include "hi5622_sta_logic.h"

/* 0 = fail closed (default), 1 = build+validate only, 2 = post. */
unsigned int hi5622_sta_join;
module_param_named(sta_join, hi5622_sta_join, uint, 0644);
MODULE_PARM_DESC(sta_join,
		 "STA join path: 0=fail closed (default), 1=build+validate the vendor WAL cfg-event join frame only, 2=post it (experimental; also needs allow_frames>0)");

#define HI5622_STA_JOIN_MAX	2u

/* Host-DRAM buffer the connect body points at, laid out as one coherent
 * allocation: bssid[6], ssid[32], key[32], ies[512]. */
#define HI5622_STA_OFF_BSSID	0u
#define HI5622_STA_OFF_SSID	8u
#define HI5622_STA_OFF_KEY	40u
#define HI5622_STA_OFF_IE	72u
#define HI5622_STA_BUF_SIZE	(HI5622_STA_OFF_IE + HI5622_STA_MAX_IE)	/* 584 */

static void hi5622_sta_free_buffer(struct hi5622_cfg80211 *c)
{
	struct hi5622_sta_ctx *s = &c->sta;

	if (s->join_buf && c->ctx && c->ctx->pdev) {
		dma_free_coherent(&c->ctx->pdev->dev, HI5622_STA_BUF_SIZE,
				  s->join_buf, (dma_addr_t)s->join_dma);
	}
	s->join_buf = NULL;
	s->join_dma = 0;
}

/* Fill the coherent buffer with the cfg80211 request data and return the
 * device-visible addresses for the body. */
static int hi5622_sta_fill_buffer(struct hi5622_cfg80211 *c,
				  struct cfg80211_connect_params *sme,
				  struct hi5622_sta_join_bufs *bufs)
{
	struct hi5622_sta_ctx *s = &c->sta;
	struct device *dev = &c->ctx->pdev->dev;
	dma_addr_t dma;
	void *buf;

	buf = dma_alloc_coherent(dev, HI5622_STA_BUF_SIZE, &dma, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memset(buf, 0, HI5622_STA_BUF_SIZE);

	/* BSSID is mandatory for a posted join (body always carries a bssid
	 * address; a NULL would hand the firmware a wild pointer). */
	memcpy((u8 *)buf + HI5622_STA_OFF_BSSID, sme->bssid, ETH_ALEN);
	memcpy((u8 *)buf + HI5622_STA_OFF_SSID, sme->ssid, sme->ssid_len);
	if (sme->key && sme->key_len)
		memcpy((u8 *)buf + HI5622_STA_OFF_KEY, sme->key, sme->key_len);
	if (sme->ie && sme->ie_len)
		memcpy((u8 *)buf + HI5622_STA_OFF_IE, sme->ie, sme->ie_len);

	hi5622_sta_free_buffer(c);
	s->join_buf = buf;
	s->join_dma = (u32)dma;

	bufs->bssid_va = s->join_dma + HI5622_STA_OFF_BSSID;
	bufs->ssid_va = s->join_dma + HI5622_STA_OFF_SSID;
	bufs->key_va = sme->key_len ? s->join_dma + HI5622_STA_OFF_KEY : 0u;
	bufs->ie_va = sme->ie_len ? s->join_dma + HI5622_STA_OFF_IE : 0u;
	bufs->ie_len = sme->ie_len;
	return 0;
}

static int hi5622_sta_build_connect(struct hi5622_cfg80211 *c,
				    struct cfg80211_connect_params *sme,
				    u8 *frame, size_t cap, size_t *frame_len,
				    bool with_buffers)
{
	struct hi5622_sta_join_bufs bufs;
	struct hi5622_sta_connect_check chk;
	u8 body[HI5622_STA_CONNECT_BODY_LEN];
	int rc;

	memset(&bufs, 0, sizeof(bufs));
	chk.ssid = sme->ssid;
	chk.ssid_len = (u32)sme->ssid_len;
	chk.bssid = sme->bssid;
	chk.ie_len = (u32)sme->ie_len;
	chk.key_len = (u32)sme->key_len;
	chk.auth_type = (u32)sme->auth_type;
	rc = hi5622_sta_connect_check(&chk);
	if (rc)
		return rc;
	if (with_buffers) {
		if (!sme->bssid) {
			dev_warn(&c->ctx->pdev->dev,
				 "sta connect: wildcard BSSID is not postable (the vendor body always carries a bssid address) - fail closed\n");
			return -EOPNOTSUPP;
		}
		rc = hi5622_sta_fill_buffer(c, sme, &bufs);
		if (rc)
			return rc;
	}
	bufs.ssid_len = (u8)sme->ssid_len;
	bufs.mfp = (u8)sme->mfp;
	bufs.privacy = sme->privacy ? 1u : 0u;
	bufs.auth_type = (u8)sme->auth_type;
	bufs.key_len = sme->key_len;
	bufs.key_idx = sme->key_idx;

	rc = hi5622_sta_connect_body(body, sizeof(body), &bufs);
	if (rc)
		return rc;
	rc = hi5622_sta_wal_frame(frame, cap, frame_len,
				  HI5622_STA_WAL_CMD_CONNECT, body,
				  sizeof(body), c->sta.seq++, 0u);
	if (rc)
		return rc;
	return hi5622_sta_frame_check(frame, *frame_len,
				      HI5622_STA_WAL_CMD_CONNECT,
				      sizeof(body));
}

int hi5622_sta_connect(struct hi5622_cfg80211 *c, struct net_device *dev,
		       struct cfg80211_connect_params *sme)
{
	u8 frame[HI5622_STA_FRAME_MAX];
	size_t frame_len = 0;
	int rc;

	if (!c || !dev || !sme || !c->ctx || !c->ctx->pdev)
		return -EINVAL;
	if (!c->wiphy_registered)
		return -ENODEV;
	if (dev->ieee80211_ptr && dev->ieee80211_ptr->iftype !=
	    NL80211_IFTYPE_STATION) {
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: only STATION interfaces are supported (iftype %d)\n",
			 dev->ieee80211_ptr->iftype);
		return -EOPNOTSUPP;
	}
	if (hi5622_sta_join > HI5622_STA_JOIN_MAX) {
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: sta_join=%u out of range (0..%u)\n",
			 hi5622_sta_join, HI5622_STA_JOIN_MAX);
		return -EINVAL;
	}
	if (!hi5622_sta_join) {
		dev_info_once(&c->ctx->pdev->dev,
			      "sta connect: host STA model validated but the firmware join is disabled (sta_join=0 fail-closed); the vendor path is WAL cfg-event opcode 0x123 - see docs/incoming/wifi-sta-static/\n");
		return -EOPNOTSUPP;
	}

	c->sta.connects++;
	rc = hi5622_sta_build_connect(c, sme, frame, sizeof(frame), &frame_len,
				      hi5622_sta_join >= 2u);
	if (rc) {
		c->sta.last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: frame build refused (%d) - no device write\n", rc);
		return rc;
	}
	if (hi5622_sta_join == 1u) {
		dev_info(&c->ctx->pdev->dev,
			 "sta connect: WAL cfg-event opcode 0x%x frame built+validated (%zu B, ssid_len=%zu auth=%d privacy=%d bssid=%pM) - dry run (sta_join=1), nothing posted\n",
			 HI5622_STA_WAL_CMD_CONNECT, frame_len, sme->ssid_len,
			 (int)sme->auth_type, sme->privacy ? 1 : 0, sme->bssid);
		c->sta.last_error = -EOPNOTSUPP;
		return -EOPNOTSUPP;
	}

	rc = hi5622_frame_post(c->ctx, frame, frame_len, "sta_connect", NULL);
	if (rc) {
		c->sta.last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: WAL join frame NOT posted (%d) - fail closed (allow_frames=%u)\n",
			 rc, hi5622_allow_frames);
		return rc;
	}
	memcpy(c->sta.bssid, sme->bssid, ETH_ALEN);
	c->sta.join_posted = true;
	dev_info(&c->ctx->pdev->dev,
		 "sta connect: WAL cfg-event opcode 0x%x posted (%zu B, ssid_len=%zu auth=%d privacy=%d bssid=%pM) - EXPERIMENTAL: completion needs the D2H management path, no cfg80211_connect_done is faked\n",
		 HI5622_STA_WAL_CMD_CONNECT, frame_len, sme->ssid_len,
		 (int)sme->auth_type, sme->privacy ? 1 : 0, c->sta.bssid);
	return 0;
}

int hi5622_sta_disconnect(struct hi5622_cfg80211 *c, struct net_device *dev,
			  u16 reason_code)
{
	struct hi5622_sta_disconnect_params p;
	u8 body[HI5622_STA_DISCONNECT_BODY_LEN];
	u8 frame[HI5622_STA_FRAME_MAX];
	size_t frame_len = 0;
	int rc;

	if (!c || !dev || !c->ctx || !c->ctx->pdev)
		return -EINVAL;
	if (!hi5622_sta_join || hi5622_sta_join > HI5622_STA_JOIN_MAX)
		return -EOPNOTSUPP;
	if (!c->sta.join_posted)
		return -ENOTCONN;

	c->sta.disconnects++;
	memset(&p, 0, sizeof(p));
	memcpy(p.bssid, c->sta.bssid, ETH_ALEN);
	p.reason_code = reason_code;
	rc = hi5622_sta_disconnect_body(body, sizeof(body), &p);
	if (rc == 0)
		rc = hi5622_sta_wal_frame(frame, sizeof(frame), &frame_len,
					  HI5622_STA_WAL_CMD_DISCONNECT, body,
					  sizeof(body), c->sta.seq++, 0u);
	if (rc == 0)
		rc = hi5622_sta_frame_check(frame, frame_len,
					    HI5622_STA_WAL_CMD_DISCONNECT,
					    sizeof(body));
	if (rc) {
		c->sta.last_error = rc;
		return rc;
	}
	if (hi5622_sta_join == 1u) {
		dev_info(&c->ctx->pdev->dev,
			 "sta disconnect: WAL cfg-event opcode 0x%x frame built+validated (%zu B, reason=%u) - dry run, nothing posted\n",
			 HI5622_STA_WAL_CMD_DISCONNECT, frame_len, reason_code);
		return -EOPNOTSUPP;
	}
	rc = hi5622_frame_post(c->ctx, frame, frame_len, "sta_disconnect", NULL);
	if (rc) {
		c->sta.last_error = rc;
		return rc;
	}
	c->sta.join_posted = false;
	dev_info(&c->ctx->pdev->dev,
		 "sta disconnect: WAL cfg-event opcode 0x%x posted (%zu B, reason=%u, bssid=%pM)\n",
		 HI5622_STA_WAL_CMD_DISCONNECT, frame_len, reason_code,
		 c->sta.bssid);
	return 0;
}

void hi5622_sta_remove(struct hi5622_cfg80211 *c)
{
	if (!c)
		return;
	hi5622_sta_free_buffer(c);
	c->sta.join_posted = false;
}
