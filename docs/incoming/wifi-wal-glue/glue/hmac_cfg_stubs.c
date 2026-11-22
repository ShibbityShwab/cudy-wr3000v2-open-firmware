/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd.
 * hi5622 wal config-command glue - hmac cfg dispatch stub surface
 * (ORIGINAL text, GPL-2.0).
 *
 * Stub surface for the hmac-side config dispatch the vendor binary
 * registers around the alg/cfg command path (names from the census CSVs in
 * wifi-framework-port/census/ and wifi-datapath-slice/mapping/):
 *
 *   hmac_event_config_syn                         (shared by name, ko-exported)
 *   hmac_config_sync_cmd_common                   (shared by name)
 *   hmac_config_sync_ssid                         (ko-only)
 *   hmac_sync_dmac_alg_cfg_rsp_entry              (ko-only)
 *   hmac_sync_dmac_alg_cfg_rsp_get_entry_normal   (ko-only)
 *   hmac_sync_dmac_cali_cfg_rsp_entry             (ko-only)
 *   hmac_chan_tx_cali_sync                        (ko-only)
 *
 * Dispatch contract from wifi-driver-re/REPORT.md s3:
 *   - D2H event entry: hmac_event_config_syn routes the config-sync
 *     responses (alg rsp / cali rsp / cali-sync), registered through the
 *     hcc_msg_register_tab_* family (plat.ko exports, s3.3).
 *   - hcc_msg_process algorithm (s3.2): header at msg_obj+0x118,
 *     group = type & 0xf, id = *(u16*)(hdr+6), handler records 16 B
 *     {id, func, aux, stats}, func at +4.
 *   - request/response pairing (s3.7): hmac_sync_dmac_alg_cfg_rsp_entry
 *     carries the per-request id word (0x0d01xxxx, 2g/5g variants) and
 *     completes the pending request via wal_set_dev_msg_response_by_addr.
 *
 * Host-only: these are pure surface stubs - no mailbox, no doorbell, no
 * device message is ever sent or consumed.  Function signatures where the
 * binary evidence is partial are PROVISIONAL and marked (same policy as
 * wifi-datapath-slice/scaffolding).
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "oal_compat.h"
#include "wal_cfg_glue.h"

/* ------------------------------------------------------------------ */
/* stub hcc registration (driver-re s3.3 customise semantics)          */
/* ------------------------------------------------------------------ */
/* handler record arrays for the cfg-response group; 12-byte input
 * records {u16 id, u32 func, u32 aux} merged into 16-byte dispatch
 * records {id, func, aux, stats}. */
#define HCC_CFG_GROUP_MAX 32
static struct wal_hcc_handler_record g_hmac_cfg_dispatch[4][HCC_CFG_GROUP_MAX];
static uint32_t g_hmac_cfg_count[4];

struct hcc_input_record {                    /* 12-byte registration record */
	uint16_t id;
	uint16_t pad;
	uint32_t func;
	uint32_t aux;
};

static int hcc_msg_customise(uint32_t group_id, void *handler_tab, uint32_t count)
{
	struct hcc_input_record *in;
	uint32_t i;

	if ((handler_tab == NULL) || (group_id >= 4) || (count > HCC_CFG_GROUP_MAX)) {
		return -1;
	}
	in = handler_tab;
	for (i = 0; i < count; i++) {
		g_hmac_cfg_dispatch[group_id][in[i].id].id = in[i].id;
		g_hmac_cfg_dispatch[group_id][in[i].id].func = in[i].func;
		g_hmac_cfg_dispatch[group_id][in[i].id].aux = in[i].aux;
		g_hmac_cfg_dispatch[group_id][in[i].id].stats = 0;
	}
	if (g_hmac_cfg_count[group_id] < count) {
		g_hmac_cfg_count[group_id] = count;
	}
	pr_info("hi5622_wal_cfg: hcc group %u registered %u handlers (stub table)\n",
		group_id, count);
	return 0;
}

/* plat.ko export names (driver-re s3.3): chip/customise/tab register one
 * group; core registers the same table into both core groups (1 and 2). */
int hcc_msg_register_tab_chip(uint32_t group_id, void *handler_tab, uint32_t count)
{
	return hcc_msg_customise(group_id, handler_tab, count);
}

int hcc_msg_register_tab(uint32_t group_id, void *handler_tab, uint32_t count)
{
	return hcc_msg_customise(group_id, handler_tab, count);
}

int hcc_msg_register_tab_customise(uint32_t group_id, void *handler_tab, uint32_t count)
{
	return hcc_msg_customise(group_id, handler_tab, count);
}

int hcc_msg_register_tab_core(uint32_t group_id, void *handler_tab, uint32_t count)
{
	int ret = hcc_msg_customise(group_id, handler_tab, count);

	if (ret == 0) {
		ret = hcc_msg_customise(group_id + 1, handler_tab, count);
	}
	return ret;
}

/* hcc_msg_process-shaped dispatch over our stub table (driver-re s3.2) */
static void wal_hcc_msg_process_stub(struct wal_hcc_reg_tab *reg_tab, void *msg_obj)
{
	struct wal_hcc_msg_header *hdr;
	uint32_t group;
	uint16_t id;
	struct wal_hcc_handler_record *entry;

	if ((reg_tab == NULL) || (msg_obj == NULL)) {
		return;
	}
	/* the header lives at object offset 0x118 in the vendor message
	 * object; the stub accepts a caller-provided header directly */
	hdr = (struct wal_hcc_msg_header *)((uint8_t *)msg_obj + 0x118);
	group = hdr->type & 0xf;
	id = *(uint16_t *)((uint8_t *)hdr + 6);
	if (group >= 4) {
		return;
	}
	if (id >= HCC_CFG_GROUP_MAX) {
		pr_warn("hi5622_wal_cfg: hcc id %u out of range\n", id);
		return;
	}
	entry = &g_hmac_cfg_dispatch[group][id];
	if (entry->func != 0) {
		void (*func)(void *, void *) = (void (*)(void *, void *))
			(unsigned long)entry->func;

		entry->stats++;
		func(NULL, msg_obj);
	} else {
		pr_warn("hi5622_wal_cfg: hcc group %u id %u unhandled\n", group, id);
	}
}

/* ------------------------------------------------------------------ */
/* the hmac cfg dispatch stub surface                                  */
/* ------------------------------------------------------------------ */

/* PROVISIONAL signature (reg_tab, msg_obj) per the hcc dispatch shape in
 * driver-re s3.2/s3.6: the firmware->host config-sync event dispatcher.
 * Routes the alg/cali rsp entries below. */
void hmac_event_config_syn(void *reg_tab, void *msg_obj)
{
	wal_hcc_msg_process_stub((struct wal_hcc_reg_tab *)reg_tab, msg_obj);
}

/* shared-by-name config-sync command helper; the vendor exports it
 * (__ksymtab). PROVISIONAL signature (board, msg_obj) -> int. */
int hmac_config_sync_cmd_common(void *board, void *msg_obj)
{
	pr_info("hi5622_wal_cfg: hmac_config_sync_cmd_common (stub)\n");
	return OAL_SUCC;
}

/* ko-only ssid sync entry; PROVISIONAL (board, msg_obj) -> int. */
int hmac_config_sync_ssid(void *board, void *msg_obj)
{
	pr_info("hi5622_wal_cfg: hmac_config_sync_ssid (stub)\n");
	return OAL_SUCC;
}

/* helper: decode the alg-cfg response payload and complete the pending
 * request.  The response header carries the per-request id word
 * (0x0d01xxxx, 2g/5g variants - driver-re s3.7); the stub copies the
 * payload into the registered response slot. */
static void hmac_sync_dmac_cfg_rsp_common(void *board, void *msg_obj,
	const char *which)
{
	struct wal_hcc_msg_header *hdr;
	uint32_t req_id;

	if (msg_obj == NULL) {
		pr_warn("hi5622_wal_cfg: %s NULL msg\n", which);
		return;
	}
	hdr = (struct wal_hcc_msg_header *)((uint8_t *)msg_obj + 0x118);
	/* params[] starts at +0x0a; the first params word is the request id */
	req_id = *(uint32_t *)hdr->params;
	if ((req_id & 0x0d010000U) == 0x0d010000U) {
		wal_set_dev_msg_response_by_addr(req_id, hdr->params + 4,
			WLAN_CFG_HOST_MAX_RSP_LEN > 64 ? 64 : WLAN_CFG_HOST_MAX_RSP_LEN);
	}
	pr_info("hi5622_wal_cfg: %s req=0x%08x (stub, no device rsp)\n",
		which, req_id);
}

void hmac_sync_dmac_alg_cfg_rsp_entry(void *board, void *msg_obj)
{
	hmac_sync_dmac_cfg_rsp_common(board, msg_obj, "alg_cfg_rsp");
}

void hmac_sync_dmac_alg_cfg_rsp_get_entry_normal(void *board, void *msg_obj)
{
	hmac_sync_dmac_cfg_rsp_common(board, msg_obj, "alg_cfg_rsp_get_normal");
}

void hmac_sync_dmac_cali_cfg_rsp_entry(void *board, void *msg_obj)
{
	hmac_sync_dmac_cfg_rsp_common(board, msg_obj, "cali_cfg_rsp");
}

/* cali-sync fallthrough of the TX path (driver-re s3.4: hmac_send_msg on
 * TX failure falls through to hmac_chan_tx_cali_sync).  Stub logs only. */
void hmac_chan_tx_cali_sync(void *board, void *msg_obj)
{
	pr_info("hi5622_wal_cfg: hmac_chan_tx_cali_sync (stub)\n");
}

/* register the cfg-response handlers into the stub hcc dispatch, exactly
 * where the vendor's hmac_main_init registers its D2H group (s3.3). */
int hmac_cfg_stubs_register(void)
{
	static const struct hcc_input_record cfg_rsp_tab[] = {
		{ 0, 0, (uint32_t)(unsigned long)hmac_sync_dmac_alg_cfg_rsp_entry, 0 },
		{ 1, 0, (uint32_t)(unsigned long)hmac_sync_dmac_alg_cfg_rsp_get_entry_normal, 0 },
		{ 2, 0, (uint32_t)(unsigned long)hmac_sync_dmac_cali_cfg_rsp_entry, 0 },
		{ 3, 0, (uint32_t)(unsigned long)hmac_chan_tx_cali_sync, 0 },
	};

	/* vendor group for the config-sync events is the core group 0
	 * (hmac_main_init registers chip groups 7/2/3; the config-syn
	 * handlers sit in the MAC event catalog group - s3.6).  Stub: core
	 * group 0, which also mirrors into group 1 per s3.3. */
	return hcc_msg_register_tab_core(0, (void *)cfg_rsp_tab,
		oal_array_size(cfg_rsp_tab));
}

EXPORT_SYMBOL(hmac_config_sync_cmd_common);
