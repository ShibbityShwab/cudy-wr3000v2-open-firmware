// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta.c - cfg80211 STA connect/disconnect for the Hi5622V100
 * clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141,
 * 2026-11-24; HARDENED by the wifi-sta-join lane, shared task task-146,
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
 * (bssid/ssid/ie/key).  The firmware dereferences them; on the vendor
 * VMSPLIT_2G kernel the raw cfg80211 params pointers happen to equal the
 * device-visible host-DRAM addresses, on our VMSPLIT_3G kernel they do not.
 * The hardened path only ever encodes dma_alloc_coherent DEVICE addresses
 * inside the bench-proven identity window (0x80000000..0x8fffffff, the same
 * window the proven SR ring nodes use), validates every address/length,
 * requires all four to be non-zero, and posts exactly once per load:
 *
 *   hi5622_sta_join=0  (default) fail closed: -EOPNOTSUPP, no frame built,
 *                       no DMA buffer, no device write
 *   hi5622_sta_join=1            build + validate + log the frame, no post
 *                                (the frame buffer IS built and checked so
 *                                the DMA-address math is exercised, but no
 *                                key material is copied into it)
 *   hi5622_sta_join=2            POST once (needs allow_frames>0); a second
 *                                connect returns -EALREADY, every post
 *                                failure is final (no retry), and the post
 *                                refuses while a firmware exception is
 *                                latched ("stop on mask/staging": firmware
 *                                CPU 0x400008 / CPU 0x40000c read by the HCC
 *                                core's exception watch).
 *
 * cfg80211_connect_done() is deliberately NOT called: the firmware's connect
 * result arrives on the D2H WAL chip-group-2 id-3 message, which this driver
 * decodes LOG-ONLY (hi5622_sta_d2h_deliver(); no cfg80211 completion is
 * faked).  The RX/AP-mgmt lanes own the cfg80211 completion wiring.
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
#include <asm/barrier.h>

#include "hi5622_cfg80211.h"
#include "hi5622_contract.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_hcc.h"
#include "hi5622_sta.h"
#include "hi5622_sta_frames.h"
#include "hi5622_sta_logic.h"

/* 0 = fail closed (default), 1 = build+validate only, 2 = one post/load. */
unsigned int hi5622_sta_join;
module_param_named(sta_join, hi5622_sta_join, uint, 0444);
MODULE_PARM_DESC(sta_join,
		 "STA join path: 0=fail closed (default), 1=build+validate the vendor WAL cfg-event join frame only (dry run, no post), 2=one single-shot post per module load (also needs allow_frames>0; no retry)");


/* ---------------------------------------------------------------------
 * Host buffer lifetime.  Zeroize before free: the key region must not be
 * left in a freed coherent page, and the firmware's [U] dereference window
 * must not be freed while the frame may still be processed.
 * -------------------------------------------------------------------*/
static void hi5622_sta_buf_free(struct hi5622_cfg80211 *c)
{
	struct hi5622_sta_ctx *s = &c->sta;

	if (s->join_buf && c->ctx && c->ctx->pdev) {
		memset(s->join_buf, 0, HI5622_STA_JOIN_BUF_SIZE);
		dma_free_coherent(&c->ctx->pdev->dev,
				  HI5622_STA_JOIN_BUF_SIZE,
				  s->join_buf, (dma_addr_t)s->join_dma);
	}
	s->join_buf = NULL;
	s->join_dma = 0;
}

/* ---------------------------------------------------------------------
 * Request validation: every cfg80211 field the body will copy is checked
 * here AND re-checked at the memcpy site (defence in depth).
 * -------------------------------------------------------------------*/
static int hi5622_sta_params_check(struct hi5622_cfg80211 *c,
				   struct cfg80211_connect_params *sme)
{
	struct hi5622_sta_connect_check chk;
	struct device *dev = &c->ctx->pdev->dev;

	memset(&chk, 0, sizeof(chk));
	chk.ssid = sme->ssid;
	chk.ssid_len = (u32)sme->ssid_len;
	chk.bssid = sme->bssid;
	chk.ie = sme->ie;
	chk.ie_len = (u32)sme->ie_len;
	chk.key = sme->key;
	chk.key_len = (u32)sme->key_len;
	chk.auth_type = (u32)sme->auth_type;
	chk.key_idx = (u32)sme->key_idx;
	if (hi5622_sta_connect_check(&chk))
		return -EINVAL;

	/* The vendor body ALWAYS carries a bssid address; a wildcard connect
	 * would need a NULL address the firmware may not accept.  Fail
	 * closed on wildcard rather than guess the [U] contract. */
	if (!sme->bssid) {
		dev_warn(dev,
			 "sta connect: wildcard BSSID is not postable (the vendor body always carries a bssid address) - fail closed\n");
		return -EOPNOTSUPP;
	}
	if (is_zero_ether_addr(sme->bssid) ||
	    is_multicast_ether_addr(sme->bssid)) {
		dev_warn(dev,
			 "sta connect: invalid BSSID %pM (zero/multicast) - fail closed\n",
			 sme->bssid);
		return -EINVAL;
	}
	return 0;
}

/* ---------------------------------------------------------------------
 * Firmware health stop condition.  The bench read pack's rules are
 *   page CA 0x1bf000 golden, state CA 0x118b60=1,
 *   mask CA 0x400008=0, staging CA 0x40000c=0.
 * The HCC core already reads mask (CPU 0x400008) and the staged vector
 * frame (CPU 0x40000c) read-only in its exception watch; a nonzero latched
 * mask or any captured vector frame is a STOP for this path (no retry).
 * -------------------------------------------------------------------*/
static int hi5622_sta_health(struct hi5622_cfg80211 *c)
{
	struct hcc_core *core;
	u32 exc_mask = 0;
	u32 staged = 0;
	int exc_valid = 0;
	int rc;

	if (!c->ctx || !c->ctx->hcc)
		return -EINVAL;
	core = &c->ctx->hcc->hcc.core;
	if (core->exception_mask_valid) {
		exc_valid = 1;
		exc_mask = core->exception_mask;
	}
	staged = core->vector_capture_count;
	rc = hi5622_sta_health_gate(exc_mask, exc_valid, staged);
	if (rc) {
		dev_warn(&c->ctx->pdev->dev,
			 "sta join: REFUSED - firmware health stop (mask=0x%08x valid=%d staged_vectors=%u); mask CA 0x400008 / staging CA 0x40000c are the bench stop rules - no retry, power-cycle if nonzero\n",
			 exc_mask, exc_valid, staged);
	}
	return rc;
}

/* ---------------------------------------------------------------------
 * Build the one coherent join buffer and validate the four body addresses.
 * with_key=false (dry run) never copies key material: the key region stays
 * zero and the frame carries key_len=0, so no secret ever reaches a buffer
 * that will not be posted.
 * -------------------------------------------------------------------*/
static int hi5622_sta_buf_build(struct hi5622_cfg80211 *c,
				struct cfg80211_connect_params *sme,
				bool with_key,
				struct hi5622_sta_join_bufs *out)
{
	struct device *dev = &c->ctx->pdev->dev;
	struct hi5622_sta_join_bufs b;
	dma_addr_t dma;
	void *buf;
	u32 dma32;
	int rc;

	memset(&b, 0, sizeof(b));
	buf = dma_alloc_coherent(dev, HI5622_STA_JOIN_BUF_SIZE, &dma,
				 GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (dma_mapping_error(dev, dma)) {
		dma_free_coherent(dev, HI5622_STA_JOIN_BUF_SIZE, buf, dma);
		return -EIO;
	}
	if ((u64)dma + HI5622_STA_JOIN_BUF_SIZE - 1u > 0xffffffffu) {
		dev_warn(dev,
			 "sta join: coherent DMA address 0x%llx does not fit u32 - fail closed\n",
			 (unsigned long long)dma);
		dma_free_coherent(dev, HI5622_STA_JOIN_BUF_SIZE, buf, dma);
		return -ERANGE;
	}
	dma32 = (u32)dma;
	if (dma32 < HI5622_HOST_DMA_LO || dma32 > HI5622_HOST_DMA_HI) {
		dev_warn(dev,
			 "sta join: coherent buffer 0x%08x outside the proven host-DMA window 0x%08x..0x%08x - fail closed\n",
			 dma32, HI5622_HOST_DMA_LO, HI5622_HOST_DMA_HI);
		dma_free_coherent(dev, HI5622_STA_JOIN_BUF_SIZE, buf, dma);
		return -ERANGE;
	}
	memset(buf, 0, HI5622_STA_JOIN_BUF_SIZE);

	/* The checks above and in hi5622_sta_params_check() bound every
	 * length; assert them again at the copy site. */
	if (sme->ssid_len == 0 || sme->ssid_len > HI5622_STA_MAX_SSID ||
	    !sme->ssid) {
		rc = -EINVAL;
		goto free;
	}
	if (sme->ie_len > HI5622_STA_MAX_IE || (sme->ie_len && !sme->ie)) {
		rc = -E2BIG;
		goto free;
	}
	if (sme->key_len > HI5622_STA_MAX_KEY ||
	    (sme->key_len && !sme->key)) {
		rc = -E2BIG;
		goto free;
	}
	memcpy((u8 *)buf + HI5622_STA_JOIN_OFF_BSSID, sme->bssid, ETH_ALEN);
	memcpy((u8 *)buf + HI5622_STA_JOIN_OFF_SSID, sme->ssid,
	       sme->ssid_len);
	if (sme->ie_len)
		memcpy((u8 *)buf + HI5622_STA_JOIN_OFF_IE, sme->ie,
		       sme->ie_len);
	if (with_key && sme->key_len)
		memcpy((u8 *)buf + HI5622_STA_JOIN_OFF_KEY, sme->key,
		       sme->key_len);

	b.bssid_va = dma32 + HI5622_STA_JOIN_OFF_BSSID;
	b.ssid_va = dma32 + HI5622_STA_JOIN_OFF_SSID;
	b.ie_va = dma32 + HI5622_STA_JOIN_OFF_IE;
	b.key_va = dma32 + HI5622_STA_JOIN_OFF_KEY;
	b.ie_len = (u32)sme->ie_len;
	b.ssid_len = (u8)sme->ssid_len;
	b.mfp = (u8)sme->mfp;
	b.privacy = sme->privacy ? 1u : 0u;
	b.auth_type = (u8)sme->auth_type;
	b.key_len = with_key ? sme->key_len : 0u;
	b.key_idx = sme->key_idx;

	rc = hi5622_sta_join_bufs_check(&b, dma32,
					HI5622_STA_JOIN_BUF_SIZE,
					HI5622_HOST_DMA_LO,
					HI5622_HOST_DMA_HI);
	if (rc)
		goto free;
	/* make the buffer contents visible to the device before the post */
	wmb();

	hi5622_sta_buf_free(c);
	c->sta.join_buf = buf;
	c->sta.join_dma = dma32;
	*out = b;
	return 0;
free:
	memset(buf, 0, HI5622_STA_JOIN_BUF_SIZE);
	dma_free_coherent(dev, HI5622_STA_JOIN_BUF_SIZE, buf, dma);
	return rc;
}

/* ---------------------------------------------------------------------
 * cfg80211 .connect
 * -------------------------------------------------------------------*/
int hi5622_sta_connect(struct hi5622_cfg80211 *c, struct net_device *dev,
		       struct cfg80211_connect_params *sme)
{
	struct hi5622_sta_join_bufs bufs;
	struct hi5622_sta_ctx *s = c ? &c->sta : NULL;
	u8 body[HI5622_STA_CONNECT_BODY_LEN];
	u8 frame[HI5622_STA_FRAME_MAX];
	size_t frame_len = 0;
	bool post;
	int rc;

	if (!c || !dev || !sme || !c->ctx || !c->ctx->pdev || !s)
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

	post = hi5622_sta_join == HI5622_STA_JOIN_POST;
	rc = hi5622_sta_join_gate(hi5622_sta_join, hi5622_allow_frames,
				  s->join_attempted ? 1 : 0);
	if (rc) {
		s->refusals++;
		s->last_error = rc;
		if (hi5622_sta_join == HI5622_STA_JOIN_OFF)
			dev_info_once(&c->ctx->pdev->dev,
				      "sta connect: host STA model validated but the firmware join is disabled (sta_join=0 fail-closed); the vendor path is WAL cfg-event opcode 0x123 - see docs/incoming/wifi-sta-join/\n");
		else
			dev_warn(&c->ctx->pdev->dev,
				 "sta connect: join gate refused (%d) sta_join=%u allow_frames=%u attempted=%d - no frame built, no device write\n",
				 rc, hi5622_sta_join, hi5622_allow_frames,
				 s->join_attempted ? 1 : 0);
		return rc;
	}
	rc = hi5622_sta_health(c);
	if (rc) {
		s->refusals++;
		s->last_error = rc;
		return rc;
	}
	if (s->join_posted) {
		s->last_error = -EALREADY;
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: already joined in this load - one join per load, no retry\n");
		return -EALREADY;
	}

	rc = hi5622_sta_params_check(c, sme);
	if (rc) {
		s->refusals++;
		s->last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: request refused (%d) - no frame built\n", rc);
		return rc;
	}
	s->connects++;

	rc = hi5622_sta_buf_build(c, sme, post, &bufs);
	if (rc) {
		s->last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: join buffer/address validation refused (%d) - no frame built, no device write\n",
			 rc);
		return rc;
	}
	rc = hi5622_sta_connect_body(body, sizeof(body), &bufs);
	if (rc == 0)
		rc = hi5622_sta_connect_body_check(body, sizeof(body), &bufs);
	if (rc == 0)
		rc = hi5622_sta_wal_frame(frame, sizeof(frame), &frame_len,
					  HI5622_STA_WAL_CMD_CONNECT, body,
					  sizeof(body), s->seq++, 0u);
	if (rc == 0)
		rc = hi5622_sta_frame_check(frame, frame_len,
					    HI5622_STA_WAL_CMD_CONNECT,
					    sizeof(body));
	if (rc) {
		s->last_error = rc;
		hi5622_sta_buf_free(c);
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: frame build/validation refused (%d) - no device write\n",
			 rc);
		return rc;
	}

	if (!post) {
		s->dry_runs++;
		dev_info(&c->ctx->pdev->dev,
			 "sta connect: WAL cfg-event opcode 0x%x frame built+validated (%zu B, ssid_len=%u auth=%d privacy=%d bssid=%pM; bssid/ssid/ie/key device addrs 0x%08x/0x%08x/0x%08x/0x%08x, key_len=%u, key NOT copied) - dry run (sta_join=1), nothing posted\n",
			 HI5622_STA_WAL_CMD_CONNECT, frame_len,
			 (unsigned int)sme->ssid_len, (int)sme->auth_type,
			 sme->privacy ? 1 : 0, sme->bssid, bufs.bssid_va,
			 bufs.ssid_va, bufs.ie_va, bufs.key_va, bufs.key_len);
		hi5622_sta_buf_free(c);
		s->last_error = -EOPNOTSUPP;
		return -EOPNOTSUPP;
	}

	/* Single-shot POST: latch BEFORE the write so a failure can never be
	 * retried in this load. */
	s->join_attempted = true;
	dev_info(&c->ctx->pdev->dev,
		 "sta connect: POSTING WAL cfg-event opcode 0x%x (%zu B, ssid_len=%u auth=%d privacy=%d bssid=%pM, key_len=%u) - single shot, no retry\n",
		 HI5622_STA_WAL_CMD_CONNECT, frame_len,
		 (unsigned int)sme->ssid_len, (int)sme->auth_type,
		 sme->privacy ? 1 : 0, sme->bssid, bufs.key_len);
	rc = hi5622_frame_post(c->ctx, frame, frame_len, "sta_connect", NULL);
	if (rc) {
		s->last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta connect: WAL join frame NOT posted (%d) - STOP, NO RETRY (one join per load); allow_frames=%u; inspect page/mask/staging, power-cycle if nonzero\n",
			 rc, hi5622_allow_frames);
		return rc;
	}
	memcpy(s->bssid, sme->bssid, ETH_ALEN);
	s->join_posted = true;
	dev_info(&c->ctx->pdev->dev,
		 "sta connect: WAL cfg-event opcode 0x%x posted (bssid=%pM) - completion arrives as D2H WAL group2 id3 and is DECODED LOG-ONLY; cfg80211_connect_done is NOT faked (RX/AP-mgmt lanes own it)\n",
		 HI5622_STA_WAL_CMD_CONNECT, s->bssid);
	return 0;
}

/* ---------------------------------------------------------------------
 * cfg80211 .disconnect (WAL opcode 0x00d0, 16-byte body)
 * -------------------------------------------------------------------*/
int hi5622_sta_disconnect(struct hi5622_cfg80211 *c, struct net_device *dev,
			  u16 reason_code)
{
	struct hi5622_sta_ctx *s = c ? &c->sta : NULL;
	struct hi5622_sta_disconnect_params p;
	u8 body[HI5622_STA_DISCONNECT_BODY_LEN];
	u8 frame[HI5622_STA_FRAME_MAX];
	size_t frame_len = 0;
	bool post;
	int rc;

	if (!c || !dev || !c->ctx || !c->ctx->pdev || !s)
		return -EINVAL;
	if (hi5622_sta_join > HI5622_STA_JOIN_MAX)
		return -EOPNOTSUPP;
	if (hi5622_sta_join == HI5622_STA_JOIN_OFF)
		return -EOPNOTSUPP;
	post = hi5622_sta_join == HI5622_STA_JOIN_POST;
	rc = hi5622_sta_health(c);
	if (rc) {
		s->refusals++;
		s->last_error = rc;
		return rc;
	}
	if (!s->join_posted)
		return -ENOTCONN;
	if (post && s->disconnect_attempted) {
		s->refusals++;
		s->last_error = -EALREADY;
		dev_warn(&c->ctx->pdev->dev,
			 "sta disconnect: already attempted in this load - no retry\n");
		return -EALREADY;
	}

	s->disconnects++;
	memset(&p, 0, sizeof(p));
	memcpy(p.bssid, s->bssid, ETH_ALEN);
	p.reason_code = reason_code;
	rc = hi5622_sta_disconnect_body(body, sizeof(body), &p);
	if (rc == 0)
		rc = hi5622_sta_wal_frame(frame, sizeof(frame), &frame_len,
					  HI5622_STA_WAL_CMD_DISCONNECT, body,
					  sizeof(body), s->seq++, 0u);
	if (rc == 0)
		rc = hi5622_sta_frame_check(frame, frame_len,
					    HI5622_STA_WAL_CMD_DISCONNECT,
					    sizeof(body));
	if (rc) {
		s->last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta disconnect: frame build/validation refused (%d) - no device write\n",
			 rc);
		return rc;
	}
	if (!post) {
		dev_info(&c->ctx->pdev->dev,
			 "sta disconnect: WAL cfg-event opcode 0x%x frame built+validated (%zu B, reason=%u) - dry run, nothing posted\n",
			 HI5622_STA_WAL_CMD_DISCONNECT, frame_len, reason_code);
		s->last_error = -EOPNOTSUPP;
		return -EOPNOTSUPP;
	}
	s->disconnect_attempted = true;
	rc = hi5622_frame_post(c->ctx, frame, frame_len, "sta_disconnect", NULL);
	if (rc) {
		s->last_error = rc;
		dev_warn(&c->ctx->pdev->dev,
			 "sta disconnect: WAL frame NOT posted (%d) - STOP, NO RETRY; inspect page/mask/staging\n",
			 rc);
		return rc;
	}
	s->join_posted = false;
	/* The disconnect is posted; the firmware is done with the join buffer
	 * of the previous connect - free it (zeroized) so the [U] dereference
	 * window does not outlive its purpose. */
	hi5622_sta_buf_free(c);
	dev_info(&c->ctx->pdev->dev,
		 "sta disconnect: WAL cfg-event opcode 0x%x posted (reason=%u, bssid=%pM) - join buffer freed\n",
		 HI5622_STA_WAL_CMD_DISCONNECT, reason_code, s->bssid);
	return 0;
}

/* ---------------------------------------------------------------------
 * D2H completion decode (log-only).  See hi5622_sta_logic.h sec 6.
 * -------------------------------------------------------------------*/
void hi5622_sta_d2h_deliver(struct hi5622_pci *ctx, const u8 *hdr,
			    const u8 *payload, u32 payload_len)
{
	u16 id;
	int rc;

	if (!ctx || !hdr || !payload || !hi5622_sta_join)
		return;
	if (!ctx->pdev)
		return;
	id = (u16)(hdr[6] | ((u16)hdr[7] << 8));

	if (id == HI5622_STA_D2H_ID_ASSOC_COMP) {
		struct hi5622_sta_assoc_comp ac;

		rc = hi5622_sta_assoc_comp_decode(hdr, HI5622_STA_D2H_HDR_LEN,
						 payload, payload_len,
						 HI5622_HOST_DMA_LO,
						 HI5622_HOST_DMA_HI, &ac);
		if (ctx->cfg)
			ctx->cfg->sta.d2h_assoc_seen++;
		if (rc) {
			dev_warn(&ctx->pdev->dev,
				 "STA D2H g2 id3 assoc-completion REFUSED by decoder (%d): 12-byte hdr + %u B payload; untrusted IE addresses/lengths not used\n",
				 rc, payload_len);
			return;
		}
		dev_info(&ctx->pdev->dev,
			 "STA D2H g2 id3 assoc-completion: status=%u bssid=%pM req_ie_len=%u resp_ie_len=%u req_ie_devva=0x%08x resp_ie_devva=0x%08x (log-only; IE addresses validated in the host-DMA window, NEVER dereferenced; cfg80211_connect_done NOT called)\n",
			 ac.status, ac.bssid, ac.req_ie_len, ac.resp_ie_len,
			 ac.req_ie_va, ac.resp_ie_va);
		return;
	}
	if (id == HI5622_STA_D2H_ID_DISASOC_COMP) {
		struct hi5622_sta_disasoc_comp dc;

		rc = hi5622_sta_disasoc_comp_decode(hdr,
						    HI5622_STA_D2H_HDR_LEN,
						    payload, payload_len, &dc);
		if (ctx->cfg)
			ctx->cfg->sta.d2h_disasoc_seen++;
		if (rc) {
			dev_warn(&ctx->pdev->dev,
				 "STA D2H g2 id4 disassoc-completion REFUSED by decoder (%d) (%u B payload)\n",
				 rc, payload_len);
			return;
		}
		dev_info(&ctx->pdev->dev,
			 "STA D2H g2 id4 disassoc-completion: reason=%u (log-only; cfg80211_disconnected NOT called)\n",
			 dc.reason);
	}
}

void hi5622_sta_remove(struct hi5622_cfg80211 *c)
{
	if (!c)
		return;
	hi5622_sta_buf_free(c);
	c->sta.join_posted = false;
}
