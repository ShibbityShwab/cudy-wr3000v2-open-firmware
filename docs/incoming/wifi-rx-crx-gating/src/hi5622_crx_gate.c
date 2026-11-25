// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_crx_gate.c - CRX delivery contract decoder + read-only firmware
 * gate probe for the Hi5622V100 clean-room host (Cudy WR3000 v2.0).
 *
 * ORIGINAL clean-room code (lane wifi-rx-crx-gating, shared task task-164).
 * No vendor source/object code, firmware byte, calibration value or per-unit
 * identity is reproduced; every constant is a derived fact cited in
 * REPORT.md with its firmware CPU address.
 *
 * The portable half (decoder + classifier) is device-free and pinned by
 * tools/crx_gate_selftest.c.  The kernel half performs READ-ONLY reads of
 * the mapped BAR0 windows (all six inbound regions are host-visible; the
 * firmware-RAM and device-IO objects are read through the same range-checked
 * translation the rest of the driver uses).  It performs NO device write,
 * allocates nothing and holds no lock across a read.
 */
#include "hi5622_crx_gate.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hi5622_driver.h"
#include "hi5622_hcc.h"
#else
#include <errno.h>
#include <string.h>
#endif

/* ===================================================================
 * 1. Strict CRX wire decode (portable)
 * =================================================================== */
int hcg_decode_crx(hcg_u32 total, const hcg_u8 *data, struct hcg_decode *out)
{
	hcg_u16 tag, len, hdr_total;
	hcg_u8 group, id, channel;
	const hcg_u8 *info;
	const hcg_u8 *frame;
	hcg_s8 rssi;

	if (!data || !out)
		return -EINVAL;
	if (total < HCG_PREFIX_LEN + HCG_MIN_FRAME_LEN)
		return -ENODATA;
	if (total > HCG_FW_TOTAL_CAP)
		return -EMSGSIZE;
	tag = (hcg_u16)data[10] | ((hcg_u16)data[11] << 8);
	if (tag != HCG_TAG)
		return -EPROTO;
	group = data[0] & 0x0fu;
	if (group != HCG_GROUP_HMAC)
		return -EPROTO;
	id = (hcg_u8)data[6] | ((hcg_u8)data[7] << 8);
	if (id != HCG_ID_CRX)
		return -EPROTO;
	hdr_total = (hcg_u16)data[4] | ((hcg_u16)data[5] << 8);
	if (hdr_total != total)
		return -EPROTO;
	info = data + HCG_HCC_HDR_LEN;
	len = (hcg_u16)info[HCG_INFO_FRAME_LEN] |
	      ((hcg_u16)info[HCG_INFO_FRAME_LEN + 1] << 8);
	if (len != total - HCG_PREFIX_LEN)
		return -EPROTO;
	if (len < HCG_MIN_FRAME_LEN || len > HCG_MAX_FRAME_LEN)
		return -EMSGSIZE;
	if (info[HCG_INFO_FLAG] == 0)
		return -ENODATA;
	channel = info[HCG_INFO_CHANNEL];
	if (!(channel == 14u || (channel >= 1u && channel <= 13u) ||
	      (channel >= 32u && channel <= 177u)))
		return -ERANGE;
	rssi = (hcg_s8)info[HCG_INFO_RSSI];
	if (rssi > 0 || rssi < -120)
		return -ERANGE;
	frame = data + HCG_PREFIX_LEN;
	if ((frame[0] & HCG_FC_VERSION_MASK) != 0u)
		return -EPROTO;
	if ((frame[0] & HCG_FC_TYPE_MASK) != 0u)
		return -EPROTO;
	out->channel = channel;
	out->rssi = rssi;
	out->frame_len = len;
	out->dst_nibble = (hcg_u8)((data[1] >> 4) & 0x0fu);
	out->frame = frame;
	return 0;
}

/* ===================================================================
 * 2. Verdict classifier (portable, pure)
 * =================================================================== */
enum hcg_verdict hcg_classify(const struct hcg_probe *p)
{
	hcg_u32 i, trigger = 0, producer = 0;
	bool mac_rx_bit;

	if (!p)
		return HCG_V_REFUSED;
	if (!p->mac_ok)
		return HCG_V_MAC_UNREADABLE;
	if (!p->fw_ok)
		return HCG_V_FW_UNREADABLE;
	for (i = 0; i < HCG_FW_DR_COUNT; i++) {
		trigger += p->fw_dr_trigger[i];
		producer |= p->fw_dr_producer[i];
	}
	mac_rx_bit = (p->mac_status & (1u << HCG_MAC_RX_STATUS_BIT)) != 0u;
	if (p->crx_seen != 0u)
		return (mac_rx_bit || p->mac_cmp_wptr != 0u) ?
		       HCG_V_CRX_AND_DATA : HCG_V_CRX_ONLY;
	if (producer == 0u && trigger == 0u && p->d2h_frames_seen == 0u)
		return HCG_V_FW_MACHINERY_IDLE;
	/* The measured silent state: the MAC prefetched its 32 free buffers
	 * at init (rptr 0x20) and never produced a completion, never raised the
	 * RX status bit.  The firmware D2H machinery is alive (ready/heartbeat
	 * frames seen) but no CRX arrived, i.e. the firmware RX input is dry. */
	if (!mac_rx_bit && p->mac_cmp_wptr == 0u &&
	    p->mac_norm_rptr == HCG_MAC_PREFETCH_EXPECTED &&
	    p->mac_small_rptr == HCG_MAC_PREFETCH_EXPECTED)
		return HCG_V_MAC_RX_IDLE;
	return HCG_V_INCONSISTENT;
}

const char *hcg_verdict_name(enum hcg_verdict v)
{
	switch (v) {
	case HCG_V_REFUSED:           return "REFUSED";
	case HCG_V_MAC_UNREADABLE:    return "MAC-UNREADABLE";
	case HCG_V_FW_UNREADABLE:     return "FW-UNREADABLE";
	case HCG_V_FW_MACHINERY_IDLE: return "FW-D2H-IDLE";
	case HCG_V_MAC_RX_IDLE:       return "MAC-RX-IDLE-UPSTREAM";
	case HCG_V_CRX_ONLY:          return "CRX-DELIVERED";
	case HCG_V_CRX_AND_DATA:      return "CRX-AND-DATA";
	case HCG_V_INCONSISTENT:      return "INCONSISTENT";
	default:                      return "UNKNOWN";
	}
}

const char *hcg_verdict_reason(enum hcg_verdict v)
{
	switch (v) {
	case HCG_V_REFUSED:
		return "probe refused (driver not armed/windows not programmed)";
	case HCG_V_MAC_UNREADABLE:
		return "MAC band0 CA 0x40050000 or mask CA 0x40040048 unreadable";
	case HCG_V_FW_UNREADABLE:
		return "firmware hcc instance/DR records unreadable through BAR0";
	case HCG_V_FW_MACHINERY_IDLE:
		return "no D2H deposit ever: firmware D2H machinery not running";
	case HCG_V_MAC_RX_IDLE:
		return "firmware D2H alive but MAC RX idle: no completion, no RX status bit2, prefetch-only 0x20 - the missing input is upstream of CRX delivery (MAC/DMAC RX hardware config)";
	case HCG_V_CRX_ONLY:
		return "group-1 id-13 CRX frames are being delivered; host-ring data still idle (expected for management-only traffic)";
	case HCG_V_CRX_AND_DATA:
		return "CRX and host-ring RX activity both present";
	case HCG_V_INCONSISTENT:
		return "unexpected state: record the raw fields before any further action";
	default:
		return "unknown verdict";
	}
}

#ifdef __KERNEL__
/* ===================================================================
 * 3. Read-only probe (kernel)
 * =================================================================== */
static hcg_u32 hcg_read_ca(struct hi5622_pci *ctx, hcg_u32 ca, bool *ok)
{
	hcg_u32 bar0 = hi5622_ca_to_bar0(ca);
	hcg_u32 v;

	if (!bar0 || !ok) {
		if (ok)
			*ok = false;
		return 0;
	}
	v = readl(ctx->bar0 + bar0);
	if (v == 0xffffffffu) {
		*ok = false;
		return 0;
	}
	return v;
}

int hi5622_crx_gate_probe(struct hi5622_pci *ctx, struct hcg_probe *p)
{
	struct hi5622_hcc_service *svc;
	bool ok = true, fw_ok = true;
	hcg_u32 i;

	if (!ctx || !p || !ctx->bar0)
		return -ENODEV;
	if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed)
		return -EBUSY;
	svc = ctx->hcc;
	memset(p, 0, sizeof(*p));

	/* MAC live bank (band 0): the RX-direction mirror of the TXQ proof. */
	p->mac_status = hcg_read_ca(ctx, HCG_MAC_CA_BAND0 + HCG_MAC_OFF_STATUS, &ok);
	p->mac_norm_rptr = hcg_read_ca(ctx,
			HCG_MAC_CA_BAND0 + HCG_MAC_OFF_NORM_RPTR, &ok);
	p->mac_small_rptr = hcg_read_ca(ctx,
			HCG_MAC_CA_BAND0 + HCG_MAC_OFF_SMALL_RPTR, &ok);
	p->mac_cmp_wptr = hcg_read_ca(ctx,
			HCG_MAC_CA_BAND0 + HCG_MAC_OFF_CMP_WPTR, &ok);
	p->mac_mask = hcg_read_ca(ctx,
			HCG_MAC_CFG_CA_BAND0 + HCG_MAC_OFF_MASK, &ok);
	p->mac_ok = ok;

	/* Firmware D2H objects: hcc instance in the ROM/WRAM inbound region. */
	p->fw_hcc_instance = hcg_read_ca(ctx, HCG_FW_HCC_INSTANCE_CA, &fw_ok);
	p->fw_dr_base = hcg_read_ca(ctx,
		HCG_FW_HCC_INSTANCE_CA + HCG_FW_INST_OFF_DR, &fw_ok);
	if (fw_ok && (p->fw_dr_base < HCG_FW_RAM_CA_LO ||
		      p->fw_dr_base > HCG_FW_RAM_CA_HI -
		      HCG_FW_DR_STRIDE * HCG_FW_DR_COUNT))
		fw_ok = false;
	if (fw_ok) {
		for (i = 0; i < HCG_FW_DR_COUNT; i++) {
			hcg_u32 rec = p->fw_dr_base + i * HCG_FW_DR_STRIDE;

			p->fw_dr_producer[i] = hcg_read_ca(ctx,
				rec + HCG_FW_DR_OFF_PRODUCER, &fw_ok);
			p->fw_dr_progress[i] = hcg_read_ca(ctx,
				rec + HCG_FW_DR_OFF_PROGRESS, &fw_ok);
			p->fw_dr_trigger[i] = hcg_read_ca(ctx,
				rec + HCG_FW_DR_OFF_TRIGGER, &fw_ok);
			p->fw_dr_released[i] = hcg_read_ca(ctx,
				rec + HCG_FW_DR_OFF_RELEASED, &fw_ok);
			if (!fw_ok)
				break;
		}
	}
	p->fw_ok = fw_ok;

	/* Host delivery side: the direct D2H catalogue already counted the
	 * frames; group 1 / id 13 is the management-RX family. */
	p->d2h_frames_seen = svc->rd.seen_count;
	for (i = 0; i < svc->rd.seen_count; i++) {
		const struct hi5622_d2h_seen *s = &svc->rd.seen[i];

		if (s->group == HCG_GROUP_HMAC && s->id == HCG_ID_CRX) {
			p->crx_seen = s->count;
			p->crx_bytes = s->last_total;
		}
	}
	return 0;
}

ssize_t hi5622_dp_crx_gate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hcg_probe p;
	enum hcg_verdict v;
	int rc, pos = 0;
	hcg_u32 i;

	if (!ctx || !ctx->dp)
		return -ENODEV;
	rc = hi5622_crx_gate_probe(ctx, &p);
	if (rc)
		return rc;
	v = hcg_classify(&p);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"crx_gate (READ-ONLY firmware delivery-gate probe; no device writes)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"verdict: %s - %s\n",
		hcg_verdict_name(v), hcg_verdict_reason(v));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"MAC band0 CA 0x%08x: status=0x%08x mask=0x%08x (expect 0x%08x)\n",
		HCG_MAC_CA_BAND0, p.mac_status, p.mac_mask,
		HCG_MAC_MASK_EXPECTED);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"MAC rings: norm_rptr=0x%08x small_rptr=0x%08x cmp_wptr=0x%08x (prefetch-only state is 0x20/0x20/0x0; RX status bit%d=%u)\n",
		p.mac_norm_rptr, p.mac_small_rptr, p.mac_cmp_wptr,
		HCG_MAC_RX_STATUS_BIT,
		(p.mac_status >> HCG_MAC_RX_STATUS_BIT) & 1u);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"firmware: hcc_instance CA 0x%08x (expect 0x%08x) dr_base CA 0x%08x readable=%u\n",
		p.fw_hcc_instance, HCG_FW_HCC_INSTANCE_CA,
		p.fw_dr_base, p.fw_ok ? 1u : 0u);
	if (p.fw_ok) {
		for (i = 0; i < HCG_FW_DR_COUNT; i++)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				"  DR%u: producer=0x%08x progress=0x%08x trigger=%u released=%u\n",
				i, p.fw_dr_producer[i], p.fw_dr_progress[i],
				p.fw_dr_trigger[i], p.fw_dr_released[i]);
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"host D2H catalogue: entries=%u crx(g1 id13)=%u last_crx_bytes=%u\n",
		p.d2h_frames_seen, p.crx_seen, p.crx_bytes);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"expected CRX wire form: total=60+frame_len, id=13, group=1, channel=info+7, len=info+0x11, rssi=info+0x27, len+60<=0x680 (firmware builder CPU 0xBA144)\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
		"note: CA 0x4016xxxx event-controller words are NOT host-visible through the six inbound regions and are refused by construction\n");
	return pos;
}
#endif /* __KERNEL__ */
