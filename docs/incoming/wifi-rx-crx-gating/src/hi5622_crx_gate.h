/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_crx_gate.h - the management-RX (CRX) delivery contract and the
 * read-only firmware-side gate probe for the Hi5622V100 clean-room host
 * (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-rx-crx-gating, shared task task-164).
 * No vendor source/object code, firmware byte, calibration value or per-unit
 * identity is reproduced: every constant below is a derived fact from the
 * local vendor disassembly lanes, cited in REPORT.md with the firmware CPU
 * address that pins it.
 *
 * Why this file exists.  The router receives no management frames although
 * the host RX rings are armed and the D2H receive service is alive.  Static
 * reversal of the firmware shows the CRX path itself is COMPLETE and
 * autonomous: the firmware builds the D2H management-RX message itself
 * (group 1 / id 13) with a strict 60 + frame_len wire form and needs no host
 * message, mask or ring to do it.  What is missing is the *input*: the
 * firmware's RX forwarding pipeline never runs because no received frame
 * reaches it.  This module implements the delivery-side contract (strict
 * frame decoder, so a CRX message can never be half-decoded) and the
 * read-only probe that attributes the silence to the MAC RX hardware, the
 * firmware RX machinery, or the host delivery path - with no device writes.
 *
 * Evidence markers: [V] decoded from the named local firmware image (CPU
 * address = file offset + 0x40000); [V*] bench-measured by an earlier lane
 * and quoted from the project reports; [I] inference; [U] unknown.
 */
#ifndef _HI5622_CRX_GATE_H
#define _HI5622_CRX_GATE_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/device.h>
struct hi5622_pci;
typedef u8  hcg_u8;
typedef u16 hcg_u16;
typedef u32 hcg_u32;
typedef s8  hcg_s8;
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  hcg_u8;
typedef uint16_t hcg_u16;
typedef uint32_t hcg_u32;
typedef int8_t   hcg_s8;
#endif

/* ===================================================================
 * 1. The CRX wire contract [V] (firmware CPU 0xBA0AA - 0xBA17A)
 *
 * The firmware builds the D2H management-RX message inside the handler at
 * CPU 0xB9780 (registered as its group-2 id-2/id-5 dispatch entry):
 *
 *   hdr[0]  group nibble = 1      (mov.w sb,#1 at 0xBA0AA, bfi at 0xBA14A)
 *   hdr[6]  id = 13               (movs r3,#0xd / strb [r4,#6] at 0xBA150)
 *   hdr[4]  total = frame_len+60  (ldrh [info+0x11] + 0x3c, 0xBA0F8,0xBA146)
 *   hdr[1]  dst nibble           (info[+2] << 4, 0xBA15C-0xBA166)
 *   hdr[2..3] metadata bytes     (info[+1], info[+0], 0xBA168-0xBA172)
 *   payload 48-byte RX info      (memcpy 0x30 B at 0xBA022-0xBA02A)
 *   payload+48 802.11 MPDU       (frame_len bytes, 0xBA050-0xBA062)
 *
 * The 48-byte RX info fields the host consumes [V]:
 *   +0x07 u8  channel            (host hmac_rx_mgmt_send_to_host msg+31)
 *   +0x11 u16 frame_len          (host cross-check data+29 vs total-60)
 *   +0x27 s8  RSSI               (host clamp(100+rssi))
 *   +0x06 u8  forward flag       (firmware drops when 0: 0xBA134)
 *   +0x02 u8  header length      (firmware compare vs len: 0xB9A84)
 *
 * Firmware-side hard cap: total = len+60 must be <= 0x680 (0xBA110-0xBA118).
 * =================================================================== */
#define HCG_GROUP_HMAC        1u    /* [V] host hmac D2H dispatch table   */
#define HCG_ID_CRX            13u   /* [V] id -> hmac_rx_process_mgmt_event */
#define HCG_HCC_HDR_LEN       12u
#define HCG_RX_INFO_LEN       48u
#define HCG_PREFIX_LEN        (HCG_HCC_HDR_LEN + HCG_RX_INFO_LEN)
#define HCG_FW_TOTAL_CAP      0x680u
#define HCG_MAX_FRAME_LEN     (HCG_FW_TOTAL_CAP - HCG_PREFIX_LEN)
#define HCG_MIN_FRAME_LEN     10u
#define HCG_TAG               0x5a5au

#define HCG_INFO_CHANNEL      0x07u
#define HCG_INFO_FRAME_LEN    0x11u
#define HCG_INFO_RSSI         0x27u
#define HCG_INFO_FLAG         0x06u
#define HCG_INFO_HDRLEN       0x02u

/* 802.11 management frame control: version 0, type 0 (management). */
#define HCG_FC_TYPE_MASK      0x0cu
#define HCG_FC_VERSION_MASK   0x03u

/* ===================================================================
 * 2. The firmware-side gate matrix (what a silent state is ranked by)
 *
 * MAC live bank = machw array word 8, CA 0x40050000 band 0 [V rx-static]:
 *   +0x00 host-MAC interrupt status, +0x08 norm free-ring rptr,
 *   +0x0c small free-ring rptr, +0x10 completion-ring wptr.
 *   RX status bit = 2 (0x04) [V shuangta_irq_host_mac_isr].
 *   Config mask CA 0x40040000 +0x48, vendor live value 0xfffffcc1.
 *
 * Firmware D2H machinery [V ete-engine / d2h-direct lanes]:
 *   hcc instance CA 0x0010C0F4 [V*], +0x14 = DR record array base CA
 *   (0x354 B per DR channel; DR0 = record 0 at CA 0x0010F2B8).
 *   record +0x0c packed producer, +0x10 progress index,
 *   +0x344 sending-trigger counter, +0x34c released-node counter.
 * The firmware image does NOT access the MAC host-interface blocks
 * 0x40040000/0x40050000 (whole-image literal census: only the constants
 * 0x40041001/0x40042000 appear, used as data/base values, never as register
 * accesses) - so those ring words are consumed by the MAC hardware only.
 * =================================================================== */
#define HCG_MAC_CA_BAND0      0x40050000u
#define HCG_MAC_CFG_CA_BAND0  0x40040000u
#define HCG_MAC_OFF_STATUS    0x00u
#define HCG_MAC_OFF_MASK      0x48u
#define HCG_MAC_OFF_NORM_RPTR 0x08u
#define HCG_MAC_OFF_SMALL_RPTR 0x0cu
#define HCG_MAC_OFF_CMP_WPTR  0x10u
#define HCG_MAC_RX_STATUS_BIT 2u
#define HCG_MAC_MASK_EXPECTED 0xfffffcc1u
#define HCG_MAC_PREFETCH_EXPECTED 0x20u

#define HCG_FW_HCC_INSTANCE_CA 0x0010C0F4u  /* [V*] bench-read golden boot */
#define HCG_FW_INST_OFF_DR     0x14u
#define HCG_FW_DR_STRIDE       0x354u
#define HCG_FW_DR_OFF_PRODUCER 0x0cu
#define HCG_FW_DR_OFF_PROGRESS 0x10u
#define HCG_FW_DR_OFF_TRIGGER  0x344u
#define HCG_FW_DR_OFF_RELEASED 0x34cu
#define HCG_FW_DR_COUNT        4u
#define HCG_FW_RAM_CA_LO       0x00000000u  /* region 0 (ROM_WRAM)      */
#define HCG_FW_RAM_CA_HI       0x001bffffu
#define HCG_IO_CA_LO          0x40000000u
#define HCG_IO_CA_HI          0x4011ffffu

/* ===================================================================
 * 3. Probe result, decode result, verdict
 * =================================================================== */
struct hcg_probe {
	hcg_u32 mac_status;
	hcg_u32 mac_mask;
	hcg_u32 mac_norm_rptr;
	hcg_u32 mac_small_rptr;
	hcg_u32 mac_cmp_wptr;
	hcg_u32 fw_hcc_instance;
	hcg_u32 fw_dr_base;
	hcg_u32 fw_dr_producer[HCG_FW_DR_COUNT];
	hcg_u32 fw_dr_progress[HCG_FW_DR_COUNT];
	hcg_u32 fw_dr_trigger[HCG_FW_DR_COUNT];
	hcg_u32 fw_dr_released[HCG_FW_DR_COUNT];
	hcg_u32 crx_seen;
	hcg_u32 crx_bytes;
	hcg_u32 d2h_frames_seen;
	bool    mac_ok;
	bool    fw_ok;
};

struct hcg_decode {
	hcg_u8  channel;
	hcg_s8  rssi;
	hcg_u16 frame_len;
	hcg_u8  dst_nibble;
	const hcg_u8 *frame;
};

enum hcg_verdict {
	HCG_V_REFUSED = 0,
	HCG_V_MAC_UNREADABLE,
	HCG_V_FW_UNREADABLE,
	HCG_V_FW_MACHINERY_IDLE,
	HCG_V_MAC_RX_IDLE,
	HCG_V_CRX_ONLY,
	HCG_V_CRX_AND_DATA,
	HCG_V_INCONSISTENT,
};

int hcg_decode_crx(hcg_u32 total, const hcg_u8 *data, struct hcg_decode *out);
enum hcg_verdict hcg_classify(const struct hcg_probe *p);
const char *hcg_verdict_name(enum hcg_verdict v);
const char *hcg_verdict_reason(enum hcg_verdict v);

#ifdef __KERNEL__
int hi5622_crx_gate_probe(struct hi5622_pci *ctx, struct hcg_probe *p);
ssize_t hi5622_dp_crx_gate_show(struct device *dev,
				struct device_attribute *attr, char *buf);
#endif

#endif /* _HI5622_CRX_GATE_H */
