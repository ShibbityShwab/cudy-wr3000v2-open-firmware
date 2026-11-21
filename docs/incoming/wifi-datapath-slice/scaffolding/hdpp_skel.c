// SPDX-License-Identifier: GPL-2.0
/*
 * hdpp_skel.c - linkable skeleton of the vendor hdpp_* host data-plane layer.
 *
 * ORIGINAL text by the wifi-datapath-slice lane (see hi5622_hdpp.h for the
 * provenance and signature policy).  Every function here is a stub that
 * returns -EOPNOTSUPP (semantics-bearing paths) or 0 (benign dfx control
 * points) and carries a TODO describing the contract that the real slice
 * must implement.  The names and export set match the vendor __ksymtab so
 * that the port (and vendor-style consumers such as wifi_debug.ko, which
 * imports 62 hdpp_/alg_ symbols) can link against this module unchanged.
 *
 * No device registers are touched by any stub: this module is safe to load
 * on the bench with zero side effects, which is exactly what the first
 * probing session should verify (UART read-only, recovery armed).
 */
#include <linux/export.h>
#include <linux/module.h>
#include "hi5622_hdpp.h"

#define HDPP_TODO(ret, fn)						\
	int32_t fn(void)						\
	{								\
		/* TODO(datapath-slice): implement per REPORT annotation */ \
		return (ret);						\
	}

/* ---------------- lifecycle ---------------- */

/* TODO: decode the boot message (vendor: dmi-style dispatch like the dpe
 * twin), then build the band/device/vap/user object tree, register the
 * handler tables into g_hdpp (wifi-driver-re section 4) and start the
 * hisi thread.  Bench probe: call with a minimal init message and watch
 * hcc/hal init logs on UART. */
int32_t hdpp_main_init(uint8_t *msg, uint16_t msg_len)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(hdpp_main_init);

/* TODO: tear the object tree down in reverse init order and stop the
 * hisi thread. */
int32_t hdpp_main_exit(void)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(hdpp_main_exit);

/* TODO: create the per-core data-path worker thread (vendor evidence:
 * kthread_create_on_node + kthread_bind + wake_up_process).  The hi1105
 * twin starts an equivalent thread from its main init. */
int32_t hdpp_hisi_thread_init(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

/* ---------------- object model ---------------- */

/* TODO(probe): returns the global board instance; vendor callers treat a
 * NULL result as fatal (warn + bail).  Allocate one instance per chip in
 * the port. */
struct hi5622_hdpp_board *hdpp_board_get_instance(uintptr_t a0, uintptr_t a1)
{
	return NULL;
}

int32_t hdpp_board_init(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_board_exit(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

struct hi5622_hdpp_chip *hdpp_chip_get_by_id(uint8_t chip_id)
{
	return NULL;
}

int32_t hdpp_chip_init(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_chip_exit(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_chip_sync_add(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_chip_add_device(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

/* TODO: band lookup (band_id, vap_id) with warn-on-miss; called by the
 * pwr_on/pwr_off config path and the band sync writers. */
struct hi5622_hdpp_band *hdpp_band_get_by_id(hi5622_band_id band_id,
					     hi5622_vap_id vap_id)
{
	return NULL;
}

int32_t hdpp_band_init(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_band_exit(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_band_module_init(void)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_band_sync_add(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_band_sync_del(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

/* TODO: in-flight TX MSDU accounting; hdpp_band_tx_msdu_inc is called from
 * the hmac TX path on enqueue, dec on completion/free. */
int32_t hdpp_band_tx_msdu_inc(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_band_tx_msdu_dec(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

struct hi5622_hdpp_device *hdpp_device_get_by_id(hi5622_device_id dev_id)
{
	return NULL;
}

int32_t hdpp_device_init(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_device_exit(void)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_device_module_init(void)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_device_add_vap(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_device_del_vap(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

/* TODO: (band_id, vap_id) lookup; 106 static call sites, all NULL-checked. */
struct hi5622_hdpp_vap *hdpp_vap_get_by_id(hi5622_band_id band_id,
					   hi5622_vap_id vap_id)
{
	return NULL;
}

int32_t hdpp_vap_init(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_del(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_module_init(void)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_module_exit(void)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_add_user(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_del_user(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

/* TODO: MAC-address lookup inside a vap (twin contract: vap_id, mac, out
 * user index).  Used by m2u/wds/hostap rx paths. */
struct hi5622_hdpp_user *hdpp_vap_find_user_by_macaddr(hi5622_vap_id vap_id,
						       const uint8_t *mac_addr)
{
	return NULL;
}

struct hi5622_hdpp_user *hdpp_user_get_by_idx(hi5622_user_idx user_idx,
					      uintptr_t a1)
{
	return NULL;
}

struct hi5622_hdpp_user *hdpp_user_get_by_addr(const uint8_t *mac_addr)
{
	return NULL;
}

/* TODO: validity test on a user slot; 45 call sites, r0 consumed as a
 * boolean in 95% of them. */
uint32_t hdpp_user_get_valid(hi5622_user_idx user_idx, uintptr_t a1)
{
	return 0;
}

int32_t hdpp_user_init(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_user_del(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_user_module_init(void)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_user_module_exit(void)
{
	return -EOPNOTSUPP;
}

/* ---------------- sync layer ---------------- */

/* TODO: acquire the read/write sync guard used around every object sync
 * write to the device (vendor evidence: _raw_spin_lock + wait-event +
 * schedule_timeout + prepare_to_wait).  The port needs an equivalent
 * host-side guard; the device side is the sync-write contract below. */
int32_t hdpp_sync_read_lock(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_sync_read_unlock(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			      uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_sync_write_lock(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_sync_write_unlock(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

/* ---------------- device sync writers ---------------- */

/* TODO: push a band/device/sys/user/vap object update to the chip through
 * the transport (hcc message or doorbell per wifi-transport-port).  These
 * are the hdpp half of the hmac "sync" contract; the bench probe is a
 * no-op sync followed by the D2H ack bitmap check. */
int32_t hdpp_band_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			     uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_device_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			       uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_sys_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_user_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			     uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_sync_read(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			   uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_vap_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			    uintptr_t a3)
{
	return -EOPNOTSUPP;
}

/* ---------------- data-path hooks ---------------- */

/* TODO: TX MSDU stat update from hmac_tx_msdu_ring_post_proc: resolve
 * device/vap by id, bump per-object counters, feed the shuangta dscr-info
 * counters. */
int32_t hdpp_tx_msdu_stat_update(uintptr_t a0, uintptr_t a1, uintptr_t a2,
				 uintptr_t a3)
{
	return -EOPNOTSUPP;
}

/* TODO: RX MSDU stat update from hmac_rx_ring_entry_proc_frame. */
int32_t hdpp_rx_msdu_stat_update(uintptr_t a0, uintptr_t a1, uintptr_t a2,
				 uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_record_sequence_of_skb(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_record_rx_sequence_of_skb(uintptr_t a0, uintptr_t a1,
				       uintptr_t a2)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_ping_diag(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_config_send_event(uintptr_t a0, uintptr_t a1)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_d2h_user_ersru_syn(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_d2h_user_twt_syn(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_rxdata_netbuf_enqueue(uintptr_t a0)
{
	return -EOPNOTSUPP;
}

/* ---------------- stat core ---------------- */

int32_t hdpp_stat_inc(uintptr_t stat_id)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_stat_packets_inc(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			      uintptr_t a3)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_stat_int64_add(uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_stat_int64_get(uintptr_t stat_id)
{
	return -EOPNOTSUPP;
}

int32_t hdpp_stat_get(uintptr_t stat_id)
{
	return -EOPNOTSUPP;
}

/* ---------------- stat dfx exported API ---------------- */
/* All 46 names below are exported exactly as in the vendor __ksymtab.
 * They are registered into the vendor debug-command framework through
 * function-pointer tables, hence the provisional signatures (see header).
 * Benign: they return 0 so a linked debug consumer keeps working while the
 * real dfx slice is pending. */

int32_t hdpp_stat_clear_hdpp(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_clear_hdpp);
int32_t hdpp_stat_dump_all(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_dump_all);
int32_t hdpp_stat_dump_hdpp(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_dump_hdpp);
int32_t hdpp_stat_dump_help(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_dump_help);
int32_t hdpp_stat_dump_item(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_dump_item);
int32_t hdpp_stat_dump_obj(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_dump_obj);

int32_t hdpp_stat_rx_irq_queue_len_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_irq_queue_len_clear);
int32_t hdpp_stat_rx_irq_queue_len_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_irq_queue_len_dump);
int32_t hdpp_stat_rx_irq_queue_len_set_stat_num(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_irq_queue_len_set_stat_num);
int32_t hdpp_stat_rx_irq_queue_len_switch(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_irq_queue_len_switch);

int32_t hdpp_stat_rx_packets_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_packets_clear);
int32_t hdpp_stat_rx_packets_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_packets_dump);
int32_t hdpp_stat_rx_packets_set_interval(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_packets_set_interval);
int32_t hdpp_stat_rx_packets_switch(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_packets_switch);

int32_t hdpp_stat_rx_ping_pong_queue_len_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_ping_pong_queue_len_clear);
int32_t hdpp_stat_rx_ping_pong_queue_len_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_ping_pong_queue_len_dump);
int32_t hdpp_stat_rx_ping_pong_queue_len_set_stat_num(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_ping_pong_queue_len_set_stat_num);
int32_t hdpp_stat_rx_ping_pong_queue_len_switch(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_ping_pong_queue_len_switch);

int32_t hdpp_stat_rx_upstream_proc_time_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_proc_time_clear);
int32_t hdpp_stat_rx_upstream_proc_time_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_proc_time_dump);
int32_t hdpp_stat_rx_upstream_proc_time_set_stat_num(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_proc_time_set_stat_num);
int32_t hdpp_stat_rx_upstream_proc_time_switch(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_proc_time_switch);

int32_t hdpp_stat_rx_upstream_queue_len_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_queue_len_clear);
int32_t hdpp_stat_rx_upstream_queue_len_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_queue_len_dump);
int32_t hdpp_stat_rx_upstream_queue_len_set_stat_num(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_queue_len_set_stat_num);
int32_t hdpp_stat_rx_upstream_queue_len_switch(uintptr_t a0, uintptr_t a1) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_upstream_queue_len_switch);

int32_t hdpp_stat_rx_wifi_stay_time_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_wifi_stay_time_clear);
int32_t hdpp_stat_rx_wifi_stay_time_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_wifi_stay_time_dump);
int32_t hdpp_stat_rx_wifi_stay_time_set_point_cnt(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_wifi_stay_time_set_point_cnt);
int32_t hdpp_stat_rx_wifi_stay_time_set_stat_num(uintptr_t a0, uintptr_t a1) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_wifi_stay_time_set_stat_num);
int32_t hdpp_stat_rx_wifi_stay_time_switch(uintptr_t a0, uintptr_t a1) { return 0; }
EXPORT_SYMBOL(hdpp_stat_rx_wifi_stay_time_switch);

int32_t hdpp_stat_tx_drop_packets_detail(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_drop_packets_detail);

int32_t hdpp_stat_tx_packets_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_packets_clear);
int32_t hdpp_stat_tx_packets_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_packets_dump);
int32_t hdpp_stat_tx_packets_set_interval(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_packets_set_interval);
int32_t hdpp_stat_tx_packets_switch(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_packets_switch);

int32_t hdpp_stat_tx_wifi_depth_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_depth_clear);
int32_t hdpp_stat_tx_wifi_depth_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_depth_dump);
int32_t hdpp_stat_tx_wifi_depth_set_stat_num(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_depth_set_stat_num);
int32_t hdpp_stat_tx_wifi_depth_switch(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_depth_switch);

int32_t hdpp_stat_tx_wifi_stay_time_clear(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_stay_time_clear);
int32_t hdpp_stat_tx_wifi_stay_time_dump(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_stay_time_dump);
int32_t hdpp_stat_tx_wifi_stay_time_set_point_cnt(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_stay_time_set_point_cnt);
int32_t hdpp_stat_tx_wifi_stay_time_set_stat_num(uintptr_t a0, uintptr_t a1) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_stay_time_set_stat_num);
int32_t hdpp_stat_tx_wifi_stay_time_set_user_id(uintptr_t a0) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_stay_time_set_user_id);
int32_t hdpp_stat_tx_wifi_stay_time_switch(uintptr_t a0, uintptr_t a1) { return 0; }
EXPORT_SYMBOL(hdpp_stat_tx_wifi_stay_time_switch);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cudy WR3000 v2.0 hi5622 hdpp_* clean-room skeleton (datapath-slice)");
