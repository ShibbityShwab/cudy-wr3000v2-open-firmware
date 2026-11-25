// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rate_ctl.c - fail-closed kernel surface for the vendor alg_cfg /
 * rate-control commands (Hi5622V100 clean-room driver, wifi-rate-ctl lane).
 *
 * Decoded contract: ../REPORT.md and ../TABLES.md.  Every wire value this
 * file can emit is a decoded vendor command (cfg id, access type, value
 * layout, blob length); the carrier is the calibration ladder's proven
 * config event (HCC group 0 / id 2 / key 0x0101), posted through the
 * wifi-h2d-send lane's exact-frame library and the same SR-node / coalesced
 * mailbox / budget / one-idea gates.  This file never writes a register
 * directly and never reads the device.
 *
 * Default OFF.  Load with rate_ctl=1 to enable the write attributes; every
 * attribute then refuses unless its command is in the decoded allowlist, the
 * per-load frame budget is respected and the recorded policy decision is
 * allow.  D2H key-0x0101 config-sync answers are decoded into the read-only
 * status/log attributes (the rate-selection observable).
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hi5622_hcc.h"
#include "hi5622_rate_ctl.h"
#include "hi5622_rate_ctl_logic.h"
#include "h2d_send.h"

/* ------------------------------------------------------------------ */
/* module parameters (fail-closed defaults)                            */
/* ------------------------------------------------------------------ */
unsigned int hi5622_rate_ctl;			/* 0 = disabled (default) */
module_param_named(rate_ctl, hi5622_rate_ctl, uint, 0644);
MODULE_PARM_DESC(rate_ctl,
		 "rate-control alg_cfg surface: 0=disabled (default), 1=enabled (decoded commands only)");

unsigned int hi5622_rate_ctl_fb = RC_CFG_FB_DEFAULT;
module_param_named(rate_ctl_fb, hi5622_rate_ctl_fb, uint, 0644);
MODULE_PARM_DESC(rate_ctl_fb,
		 "config-event header field B (module context): 0..0x12, default 4 (the module the type-1 context-create made)");

unsigned int hi5622_rate_ctl_arbitrary;		/* 0 = no raw probe (default) */
module_param_named(rate_ctl_arbitrary, hi5622_rate_ctl_arbitrary, uint, 0644);
MODULE_PARM_DESC(rate_ctl_arbitrary,
		 "allow the rate_ctl_send raw probe attribute for documented ids: 0=off (default), 1=on");

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */
struct hi5622_rate_ctl_state {
	struct mutex lock;
	u32 sent;
	u32 refused;
	u32 rsp;
	u32 last_op;
	u32 last_decision;
	u16 last_cfg_id;
	u8  last_cfg_type;
	u16 last_blob_len;
	u8  last_blob[RC_ALG_MAX_BLOB];
	u16 last_rsp_body_len;
	u8  last_rsp_body[RC_ALG_MAX_BLOB];
	struct rc_sync last_sync;	/* valid when have_sync */
	bool have_sync;
};

static struct hi5622_rate_ctl_state rc_state;
static bool rc_inited;

static struct hi5622_pci *rc_ctx(struct device *dev)
{
	return pci_get_drvdata(to_pci_dev(dev));
}

static void rc_policy_fill(struct rc_policy *pol, struct hi5622_pci *ctx)
{
	u32 budget;

	memset(pol, 0, sizeof(*pol));
	pol->enabled = hi5622_rate_ctl != 0u;
	pol->arbitrary = hi5622_rate_ctl_arbitrary != 0u;
	budget = hi5622_h2d_send_budget > 8u ? 8u : hi5622_h2d_send_budget;
	pol->budget = budget;
	if (ctx && ctx->hcc)
		pol->sent = ctx->hcc->h2d_frames_sent;
}

/* ------------------------------------------------------------------ */
/* sysfs grammar helpers                                               */
/* ------------------------------------------------------------------ */
static const char *rc_skip_spaces(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

static int rc_parse_uint(const char **pp, unsigned long *out)
{
	const char *p = rc_skip_spaces(*pp);
	unsigned long v = 0;
	unsigned base = 10;
	bool any = false;

	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
	}
	while (*p) {
		unsigned d;

		if (*p >= '0' && *p <= '9')
			d = (unsigned)(*p - '0');
		else if (base == 16 && *p >= 'a' && *p <= 'f')
			d = (unsigned)(*p - 'a') + 10u;
		else if (base == 16 && *p >= 'A' && *p <= 'F')
			d = (unsigned)(*p - 'A') + 10u;
		else
			break;
		if (d >= base)
			break;
		if (v > (0xfffffffful - d) / base)
			return -ERANGE;
		v = v * base + d;
		p++;
		any = true;
	}
	if (!any)
		return -EINVAL;
	*out = v;
	*pp = p;
	return 0;
}

static bool rc_rest_is_space(const char *p)
{
	p = rc_skip_spaces(p);
	return *p == '\0';
}

static int rc_parse_token(const char **pp, char *out, size_t cap)
{
	const char *p = rc_skip_spaces(*pp);
	size_t n = 0;

	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
		if (n + 1u >= cap)
			return -ERANGE;
		out[n++] = *p++;
	}
	if (!n)
		return -EINVAL;
	out[n] = '\0';
	*pp = p;
	return 0;
}

static int rc_parse_hex_pairs(const char **pp, u8 *dst, u16 *out_len,
			      size_t max)
{
	const char *p = rc_skip_spaces(*pp);
	size_t n = 0;

	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
		int hi, lo;

		if (p[0] >= '0' && p[0] <= '9')
			hi = p[0] - '0';
		else if (p[0] >= 'a' && p[0] <= 'f')
			hi = p[0] - 'a' + 10;
		else if (p[0] >= 'A' && p[0] <= 'F')
			hi = p[0] - 'A' + 10;
		else
			return -EINVAL;
		if (!p[1])
			return -EINVAL;
		if (p[1] >= '0' && p[1] <= '9')
			lo = p[1] - '0';
		else if (p[1] >= 'a' && p[1] <= 'f')
			lo = p[1] - 'a' + 10;
		else if (p[1] >= 'A' && p[1] <= 'F')
			lo = p[1] - 'A' + 10;
		else
			return -EINVAL;
		if (n >= max)
			return -ERANGE;
		dst[n++] = (u8)((hi << 4) | lo);
		p += 2;
	}
	if (!n)
		return -EINVAL;
	*out_len = (u16)n;
	*pp = p;
	return 0;
}

/* ------------------------------------------------------------------ */
/* the send path                                                       */
/* ------------------------------------------------------------------ */
static int rc_send(struct hi5622_pci *ctx, unsigned int op,
		   const struct rc_req *req)
{
	struct rc_policy pol;
	size_t blob_len = 0;
	int rc;

	if (!ctx || !ctx->hcc)
		return -ENODEV;
	rc_policy_fill(&pol, ctx);
	if (rc_check(&pol, req, (enum rc_op)op) != RC_DEC_ALLOW) {
		rc_state.refused++;
		rc_state.last_op = op;
		rc_state.last_decision = (u32)rc_check(&pol, req, (enum rc_op)op);
		dev_warn(&ctx->pdev->dev,
			 "RATE_CTL: op %u cfg %u refused (%s); param rate_ctl=%u arbitrary=%u sent=%u/%u - no device write\n",
			 op, req->cfg_id,
			 rc_name_decision((enum rc_decision)rc_state.last_decision),
			 hi5622_rate_ctl, hi5622_rate_ctl_arbitrary,
			 pol.sent, pol.budget);
		return -EPERM;
	}
	if (rc_blob_build(req, rc_state.last_blob, sizeof(rc_state.last_blob),
			  &blob_len) != 0) {
		rc_state.refused++;
		rc_state.last_decision = (u32)RC_DEC_BAD_LEN;
		return -EINVAL;
	}
	rc_state.last_op = op;
	rc_state.last_decision = (u32)RC_DEC_ALLOW;
	rc_state.last_cfg_id = req->cfg_id;
	rc_state.last_cfg_type = req->cfg_type;
	rc_state.last_blob_len = (u16)blob_len;
	if (hi5622_rate_ctl_fb > RC_CFG_FB_MAX)
		return -ERANGE;
	rc = h2d_post_rate_cfg(ctx, req->cfg_id, req->cfg_type, req->pkt_type,
			       req->value, req->value_len,
			       (u8)hi5622_rate_ctl_fb);
	if (rc == 0) {
		rc_state.sent++;
		dev_info(&ctx->pdev->dev,
			 "RATE_CTL: sent cfg %u type %s (op %u) alg_cfg blob %zu B on the g0/id2 key-0x0101 config event (fb=%u); waiting for the g1 id3 sync answer\n",
			 req->cfg_id, req->cfg_type ? "GET" : "SET", op,
			 blob_len, hi5622_rate_ctl_fb);
		return 0;
	}
	rc_state.refused++;
	dev_warn(&ctx->pdev->dev,
		 "RATE_CTL: cfg %u type %s post refused (%d); one idea per load, no retry - see dmesg for the gate\n",
		 req->cfg_id, req->cfg_type ? "GET" : "SET", rc);
	return rc;
}

/* ------------------------------------------------------------------ */
/* write attributes                                                    */
/* ------------------------------------------------------------------ */
static ssize_t rc_rate_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	unsigned long v;
	int rc;

	if (!sysfs_streq(buf, "auto") && !sysfs_streq(buf, "fixed") &&
	    !sysfs_streq(buf, "0") && !sysfs_streq(buf, "1"))
		return -EINVAL;
	if (sysfs_streq(buf, "auto") || sysfs_streq(buf, "0"))
		v = RC_RATE_MODE_AUTO;
	else
		v = RC_RATE_MODE_FIXED;
	if (rc_req_rate_mode(&req, (unsigned)v) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_RATE_MODE_SET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_rate_mode);

static ssize_t rc_rate_mode_get_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	int rc;

	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	if (rc_req_rate_mode_get(&req) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_RATE_MODE_GET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_rate_mode_get);

static ssize_t rc_ar_set_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	char knob[40];
	const char *p = buf;
	unsigned long value;
	int rc;

	rc = rc_parse_token(&p, knob, sizeof(knob));
	if (rc)
		return rc;
	rc = rc_parse_uint(&p, &value);
	if (rc || !rc_rest_is_space(p))
		return -EINVAL;
	if (value > 0xfffffffful)
		return -ERANGE;
	if (rc_req_ar_dev_param(&req, knob, value) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_AR_SET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_ar_set);

static ssize_t rc_ar_get_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	char knob[40];
	const char *p = buf;
	int rc;

	rc = rc_parse_token(&p, knob, sizeof(knob));
	if (rc || !rc_rest_is_space(p))
		return -EINVAL;
	if (rc_req_ar_dev_param_get(&req, knob) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_AR_GET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_ar_get);

static ssize_t rc_tpc_pow_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	char mode[16];
	const char *p = buf;
	unsigned long level;
	unsigned m;
	int rc;

	rc = rc_parse_token(&p, mode, sizeof(mode));
	if (rc)
		return rc;
	rc = rc_parse_uint(&p, &level);
	if (rc || !rc_rest_is_space(p))
		return -EINVAL;
	if (strcmp(mode, "data") == 0)
		m = RC_TPC_POW_DATA;
	else if (strcmp(mode, "mag") == 0)
		m = RC_TPC_POW_MAG;
	else if (strcmp(mode, "ctrl") == 0)
		m = RC_TPC_POW_CTRL;
	else {
		unsigned long mv;

		if (rc_parse_uint((const char **)&mode, &mv) != 0 || mv > 2ul)
			return -EINVAL;
		m = (unsigned)mv;
	}
	if (level > 0xfful)
		return -ERANGE;
	if (rc_req_tpc_pow_lvl(&req, m, (unsigned)level) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_TPC_POW_LVL_SET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_tpc_pow);

static ssize_t rc_tpc_get_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	const char *p = buf;
	unsigned long id;
	int rc;

	rc = rc_parse_uint(&p, &id);
	if (rc || !rc_rest_is_space(p))
		return -EINVAL;
	if (id > 0xfffful)
		return -ERANGE;
	if (rc_req_tpc_get(&req, (u16)id) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_TPC_GET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_tpc_get);

static ssize_t rc_ration_get_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	unsigned int m[6];
	int rc;

	if (sscanf(buf, "%2x:%2x:%2x:%2x:%2x:%2x",
		   &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
		return -EINVAL;
	{
		u8 mac[6];

		for (rc = 0; rc < 6; rc++)
			mac[rc] = (u8)(m[rc] & 0xffu);
		if (rc_req_ration_get(&req, mac) != 0)
			return -EINVAL;
	}
	rc = rc_send(ctx, RC_OP_RATION_GET, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_ration_get);

static ssize_t rc_send_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_req req;
	u8 value[RC_ALG_MAX_VALUE];
	u16 value_len = 0;
	unsigned long cfg_id = 0, pkt = 0;
	bool have_id = false, have_type = false, have_pkt = false;
	bool is_get = false;
	const char *p = buf;
	int rc;

	while (!rc_rest_is_space(p)) {
		char tok[48];

		rc = rc_parse_token(&p, tok, sizeof(tok));
		if (rc)
			return rc;
		if (strncmp(tok, "id=", 3) == 0) {
			const char *q = tok + 3;

			if (rc_parse_uint(&q, &cfg_id) || *q)
				return -EINVAL;
			have_id = true;
		} else if (strncmp(tok, "type=", 5) == 0) {
			if (strcmp(tok + 5, "set") == 0)
				is_get = false;
			else if (strcmp(tok + 5, "get") == 0)
				is_get = true;
			else
				return -EINVAL;
			have_type = true;
		} else if (strncmp(tok, "pkt=", 4) == 0) {
			const char *q = tok + 4;

			if (rc_parse_uint(&q, &pkt) || *q)
				return -EINVAL;
			have_pkt = true;
		} else if (strncmp(tok, "value=", 6) == 0) {
			const char *q = tok + 6;

			rc = rc_parse_hex_pairs(&q, value, &value_len,
						sizeof(value));
			if (rc)
				return rc;
		} else {
			return -EINVAL;
		}
	}
	if (!have_id || !have_type || cfg_id > 0xfffful)
		return -EINVAL;
	if (!have_pkt)
		pkt = RC_PKT_NONE;
	if (pkt > RC_PKT_NONE)
		return -EINVAL;
	if (rc_req_raw(&req, (u16)cfg_id, is_get ? RC_CFG_GET : RC_CFG_SET,
		       (u8)pkt, value_len ? value : NULL, value_len) != 0)
		return -EINVAL;
	rc = rc_send(ctx, RC_OP_ARBITRARY, &req);
	return rc ? rc : (ssize_t)count;
}
static DEVICE_ATTR_WO(rc_send);

/* ------------------------------------------------------------------ */
/* read-only status / log                                             */
/* ------------------------------------------------------------------ */
static int rc_hex(char *out, size_t cap, const u8 *p, u16 len)
{
	static const char digits[] = "0123456789abcdef";
	size_t i, pos = 0;

	if (!cap)
		return 0;
	for (i = 0; i < len; i++) {
		if (pos + 3u > cap)
			break;
		out[pos++] = digits[p[i] >> 4];
		out[pos++] = digits[p[i] & 0x0fu];
		if (i + 1u < len)
			out[pos++] = ' ';
	}
	out[pos] = '\0';
	return (int)pos;
}

static ssize_t rc_status_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = rc_ctx(dev);
	struct rc_policy pol;
	char blob[3 * 64];
	char rsp[3 * 64];
	int n = 0;

	rc_policy_fill(&pol, ctx);
	mutex_lock(&rc_state.lock);
	rc_hex(blob, sizeof(blob), rc_state.last_blob,
	       rc_state.last_blob_len > 64u ? 64u : rc_state.last_blob_len);
	rc_hex(rsp, sizeof(rsp), rc_state.last_rsp_body,
	       rc_state.last_rsp_body_len > 64u ? 64u : rc_state.last_rsp_body_len);
	n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
		       "rate_ctl=%u rate_ctl_fb=%u rate_ctl_arbitrary=%u enabled=%u arbitrary=%u budget=%u committed=%u/%u refused=%u rsp=%u\n",
		       hi5622_rate_ctl, hi5622_rate_ctl_fb,
		       hi5622_rate_ctl_arbitrary, pol.enabled ? 1u : 0u,
		       pol.arbitrary ? 1u : 0u, pol.budget,
		       rc_state.sent, pol.budget,
		       rc_state.refused, rc_state.rsp);
	n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
		       "last_op=%u last_decision=%s last_cfg=%u last_type=%s last_blob_len=%u\n",
		       rc_state.last_op,
		       rc_name_decision((enum rc_decision)rc_state.last_decision),
		       rc_state.last_cfg_id,
		       rc_state.last_cfg_type ? "GET" : "SET",
		       rc_state.last_blob_len);
	if (rc_state.last_blob_len)
		n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
			       "last_blob=%s%s\n", blob,
			       rc_state.last_blob_len > 64u ? " ..." : "");
	if (rc_state.have_sync) {
		char val[3 * 16];
		const struct rc_sync *s = &rc_state.last_sync;

		rc_hex(val, sizeof(val), s->value,
		       s->value_len > 16u ? 16u : s->value_len);
		n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
			       "last_rsp syn=0x0101 cfg=%u type=%s pkt=%u dev_ret=%u need_w4=%u body_len=%u value_len=%u value=%s\n",
			       s->cfg_id, s->cfg_type ? "GET" : "SET",
			       s->pkt_type, s->dev_ret, s->need_w4,
			       s->body_len, s->value_len, val);
	}
	if (rc_state.last_rsp_body_len)
		n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
			       "last_rsp_body=%s%s\n", rsp,
			       rc_state.last_rsp_body_len > 64u ? " ..." : "");
	mutex_unlock(&rc_state.lock);
	return n;
}
static DEVICE_ATTR_RO(rc_status);

static ssize_t rc_log_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	char blob[3 * 97];
	char body[3 * 97];
	int n = 0;

	mutex_lock(&rc_state.lock);
	rc_hex(blob, sizeof(blob), rc_state.last_blob,
	       rc_state.last_blob_len > 96u ? 96u : rc_state.last_blob_len);
	rc_hex(body, sizeof(body), rc_state.last_rsp_body,
	       rc_state.last_rsp_body_len > 96u ? 96u :
	       rc_state.last_rsp_body_len);
	n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
		       "request_blob[%u]=%s\n", rc_state.last_blob_len, blob);
	n += scnprintf(buf + n, PAGE_SIZE - (size_t)n,
		       "response_body[%u]=%s\n", rc_state.last_rsp_body_len, body);
	mutex_unlock(&rc_state.lock);
	return n;
}
static DEVICE_ATTR_RO(rc_log);

static struct attribute *hi5622_rate_ctl_attrs[] = {
	&dev_attr_rc_rate_mode.attr,
	&dev_attr_rc_rate_mode_get.attr,
	&dev_attr_rc_ar_set.attr,
	&dev_attr_rc_ar_get.attr,
	&dev_attr_rc_tpc_pow.attr,
	&dev_attr_rc_tpc_get.attr,
	&dev_attr_rc_ration_get.attr,
	&dev_attr_rc_send.attr,
	&dev_attr_rc_status.attr,
	&dev_attr_rc_log.attr,
	NULL,
};

static const struct attribute_group hi5622_rate_ctl_group = {
	.attrs = hi5622_rate_ctl_attrs,
};

/* ------------------------------------------------------------------ */
/* D2H config-sync capture (the rate-selection observable)             */
/* ------------------------------------------------------------------ */
void hi5622_rate_ctl_rx(struct hi5622_pci *ctx, const u8 *hdr,
			const u8 *payload, u32 payload_len)
{
	struct rc_sync sync;
	u8 group, id;

	if (!ctx || !hdr || !payload)
		return;
	if (payload_len > RC_ALG_MAX_BLOB + 32u)
		payload_len = RC_ALG_MAX_BLOB + 32u;
	group = (u8)(hdr[0] & 0x0fu);
	id = (u8)(hdr[6] | (hdr[7] << 8));
	if (group != 1u || id != 3u)
		return;
	if (rc_parse_cfg_sync(group, id, payload, payload_len, &sync) != 0)
		return;
	mutex_lock(&rc_state.lock);
	rc_state.rsp++;
	rc_state.last_sync = sync;
	rc_state.have_sync = true;
	{
		/* the response body starts after the 12-byte cfg-sync wrapper */
		u16 body_len = (u16)(payload[10] | ((u16)payload[11] << 8));

		if (body_len <= RC_ALG_MAX_BLOB) {
			memcpy(rc_state.last_rsp_body, payload + 12, body_len);
			rc_state.last_rsp_body_len = body_len;
		}
	}
	mutex_unlock(&rc_state.lock);
	dev_info(&ctx->pdev->dev,
		 "RATE_CTL rsp: g1 id3 syn 0x0101 cfg %u type %s dev_ret %u value_len %u\n",
		 sync.cfg_id, sync.cfg_type ? "GET" : "SET", sync.dev_ret,
		 sync.value_len);
}

/* ------------------------------------------------------------------ */
/* registration                                                        */
/* ------------------------------------------------------------------ */
int hi5622_rate_ctl_add_attrs(struct device *dev)
{
	int rc;

	if (!rc_inited) {
		memset(&rc_state, 0, sizeof(rc_state));
		mutex_init(&rc_state.lock);
		rc_inited = true;
	}
	rc = sysfs_create_group(&dev->kobj, &hi5622_rate_ctl_group);
	if (rc)
		dev_warn(dev, "RATE_CTL: sysfs group not created (%d)\n", rc);
	else if (hi5622_rate_ctl)
		dev_info(dev,
			 "RATE_CTL: enabled (fb=%u arbitrary=%u); rate_mode/rate_mode_get/ar_set/ar_get/tpc_pow/tpc_get/ration_get/rc_send are the decoded alg_cfg commands\n",
			 hi5622_rate_ctl_fb, hi5622_rate_ctl_arbitrary);
	return rc;
}

void hi5622_rate_ctl_del_attrs(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &hi5622_rate_ctl_group);
}