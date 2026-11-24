/* SPDX-License-Identifier: GPL-2.0 */
/* hi5622_band_cfg.c - refined g0 id10 builder (wifi-scan-full, task-138).
 * Clean-room; field map from the firmware handler CA 0x5FF64 and the vendor
 * host builders hmac_mgmt_rx_addba_rsp (0x45A94) / hmac_mgmt_rx_delba
 * (0x45D88) in hi5622v100_wifi.ko 2.5.24.  Nothing is sent unless the
 * scan_band_cfg module parameter is enabled on the bench. */
#include "hi5622_band_cfg.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

static void put_le16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)(v >> 8);
}

int hi5622_band_cfg_build(u8 *out, size_t cap, size_t *len,
			  const struct hi5622_band_cfg_params *p)
{
	u8 pl[HI5622_BAND_CFG_PAYLOAD_LEN];

	if (!out || !p || cap < HI5622_BAND_CFG_TOTAL)
		return -EINVAL;
	if (p->subtype != HI5622_BAND_CFG_SUB_SLOT &&
	    p->subtype != HI5622_BAND_CFG_SUB_CLEAR)
		return -EINVAL;
	if (p->slot > 7u)	/* firmware logs and refuses slots > 7 */
		return -EINVAL;
	memset(out, 0, HI5622_BAND_CFG_TOTAL);
	memset(pl, 0, sizeof(pl));

	/* 12-byte HCC header: same g0 direct shape as the scan request we
	 * already post on the bench (group 0, alloc=1, dst 0, tag 0x5A5A).
	 * Header[3] = 0 lets the firmware resolve module 0 (2.4 GHz VAP). */
	out[0] = 0x00u;
	out[1] = 0x01u;			/* VS_ALLOC_NIBBLE | dst<<4 */
	out[2] = 0x00u;			/* vap id 0 */
	out[3] = 0x00u;			/* chip 0 (handler resolves module) */
	put_le16(out + 4, (u16)HI5622_BAND_CFG_TOTAL);
	out[6] = HI5622_BAND_CFG_ID;
	out[7] = 0x00u;
	out[8] = 0x00u;
	out[9] = 0x00u;
	out[10] = 0x5au;
	out[11] = 0x5au;

	pl[0x00] = HI5622_BAND_CFG_TYPE;
	pl[0x01] = p->subtype;
	put_le16(pl + 0x02, p->chan_id);
	pl[0x07] = p->slot;

	if (p->subtype == HI5622_BAND_CFG_SUB_SLOT) {
		/* addba-rsp relay shape (vendor builder writes the same set:
		 * +0x09, +0x0C, +0x10, +0x12, +0x14; +0x08/+0x15 not used). */
		pl[0x09] = p->action;
		put_le16(pl + 0x0c, p->start);
		put_le16(pl + 0x10, p->field10);
		pl[0x12] = p->mode;
		put_le16(pl + 0x14, p->cmp);
	} else {
		/* delba relay shape: the firmware reads payload+0x14 (byte)
		 * as the clear/remove flag.  0 = clear the slot context. */
		pl[0x14] = p->sub2_flag;
	}

	memcpy(out + 12, pl, sizeof(pl));
	if (len)
		*len = HI5622_BAND_CFG_TOTAL;
	return 0;
}
