/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_hdpp.h - clean-room public surface of the vendor hdpp_* layer
 *
 * This header is ORIGINAL text written by the wifi-datapath-slice lane of the
 * Cudy WR3000 v2.0 notebook from decoded interface contracts only: the vendor
 * hi5622v100_wifi.ko symbol table (238 hdpp_* functions, 52,428 B, all LOCAL
 * symbols; 46 names re-exported through __ksymtab), the bl-call graph
 * recovered from that binary, and the hi1105 dpe_* engine used as a
 * NAME/STRUCTURE reference only (hongmengkernel mirror carries no license
 * grant; none of its text is reproduced here).  No vendor source text, object
 * code, or firmware blob is embedded.
 *
 * Signature policy (documented so the hmac/wal port slices can link):
 *  - int32_t return everywhere (the vendor ABI uses s32 returns; call-site
 *    r0-consumption evidence agrees for most functions);
 *  - parameter counts come from measured call sites (mapping/hdpp-inventory.csv
 *    arity_* columns); where no static call site exists (function-pointer
 *    registration only) the declaration uses one uintptr_t argument and is
 *    marked PROVISIONAL;
 *  - the object-model getters return opaque pointers, inferred from the
 *    get-by-id usage pattern (r0 checked against NULL by every caller).
 *
 * The remaining hdpp_* symbols not declared here are internal helpers of the
 * vendor build; their names/sizes are inventoried in
 * ../mapping/hdpp-inventory.csv and should be declared when the slice that
 * needs them lands.
 */
#ifndef HI5622_HDPP_H
#define HI5622_HDPP_H

#include <linux/types.h>

/* Opaque handles to the vendor's host data-plane objects.  Layout is unknown
 * on purpose: the port must never touch these structs, only pass pointers.
 */
struct hi5622_hdpp_band;
struct hi5622_hdpp_board;
struct hi5622_hdpp_chip;
struct hi5622_hdpp_device;
struct hi5622_hdpp_user;
struct hi5622_hdpp_vap;

typedef uint8_t hi5622_band_id;
typedef uint8_t hi5622_device_id;
typedef uint8_t hi5622_vap_id;
typedef uint16_t hi5622_user_idx;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/* hdpp_main_init: decode the boot message and build the hdpp object tree.
 * dpe twin: dpe_main_init (dpe/main/dpe_main.c).  The vendor call site from
 * hmac_main_init passes no registers, so the two parameters below follow the
 * dpe shape (message buffer + length); the decoded vendor init sequence
 * additionally registers handler tables into the g_hdpp global
 * (wifi-driver-re REPORT section 4).  TODO(probe): confirm param passing.
 */
int32_t hdpp_main_init(uint8_t *msg, uint16_t msg_len);
int32_t hdpp_main_exit(void);

/* Data-path worker thread (vendor: kthread_create_on_node + kthread_bind,
 * created from hdpp_main_init).  dpe structural twin: the hisi_thread init
 * called from dpe_main_init (hmac_hisi_thread_init in the hi1105 tree).
 * TODO(probe): identify the two parameters (likely core mask / thread name
 * or cpu id + cookie). */
int32_t hdpp_hisi_thread_init(uintptr_t a0, uintptr_t a1);

/* ------------------------------------------------------------------ */
/* Object model (board/chip/band/device/vap/user resource tables)      */
/* ------------------------------------------------------------------ */

struct hi5622_hdpp_board *hdpp_board_get_instance(uintptr_t a0, uintptr_t a1);
int32_t hdpp_board_init(uintptr_t a0);
int32_t hdpp_board_exit(uintptr_t a0);

struct hi5622_hdpp_chip *hdpp_chip_get_by_id(uint8_t chip_id);
int32_t hdpp_chip_init(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3);
int32_t hdpp_chip_exit(uintptr_t a0, uintptr_t a1, uintptr_t a2);
int32_t hdpp_chip_sync_add(uintptr_t a0);
int32_t hdpp_chip_add_device(uintptr_t a0, uintptr_t a1, uintptr_t a2);

struct hi5622_hdpp_band *hdpp_band_get_by_id(hi5622_band_id band_id,
					     hi5622_vap_id vap_id);
int32_t hdpp_band_init(uintptr_t a0, uintptr_t a1);
int32_t hdpp_band_exit(uintptr_t a0);
int32_t hdpp_band_module_init(void);
int32_t hdpp_band_sync_add(uintptr_t a0);
int32_t hdpp_band_sync_del(uintptr_t a0);
/* Band-wide TX MSDU in-flight accounting, called from the hmac TX ring code. */
int32_t hdpp_band_tx_msdu_inc(uintptr_t a0, uintptr_t a1);
int32_t hdpp_band_tx_msdu_dec(uintptr_t a0, uintptr_t a1, uintptr_t a2);

struct hi5622_hdpp_device *hdpp_device_get_by_id(hi5622_device_id dev_id);
int32_t hdpp_device_init(uintptr_t a0);
int32_t hdpp_device_exit(void);
int32_t hdpp_device_module_init(void);
int32_t hdpp_device_add_vap(uintptr_t a0);
int32_t hdpp_device_del_vap(uintptr_t a0);

struct hi5622_hdpp_vap *hdpp_vap_get_by_id(hi5622_band_id band_id,
					   hi5622_vap_id vap_id);
int32_t hdpp_vap_init(uintptr_t a0, uintptr_t a1);
int32_t hdpp_vap_del(uintptr_t a0);
int32_t hdpp_vap_module_init(void);
int32_t hdpp_vap_module_exit(void);
int32_t hdpp_vap_add_user(uintptr_t a0, uintptr_t a1, uintptr_t a2);
int32_t hdpp_vap_del_user(uintptr_t a0);
/* Twin: dpe_vap_find_user_by_macaddr(dpe/resource/dpe_wlan_user.c),
 * shape (vap_id, sta_mac_addr, out user_idx). */
struct hi5622_hdpp_user *hdpp_vap_find_user_by_macaddr(hi5622_vap_id vap_id,
						       const uint8_t *mac_addr);

struct hi5622_hdpp_user *hdpp_user_get_by_idx(hi5622_user_idx user_idx,
					      uintptr_t a1);
struct hi5622_hdpp_user *hdpp_user_get_by_addr(const uint8_t *mac_addr);
uint32_t hdpp_user_get_valid(hi5622_user_idx user_idx, uintptr_t a1);
int32_t hdpp_user_init(uintptr_t a0, uintptr_t a1, uintptr_t a2,
		       uintptr_t a3);
int32_t hdpp_user_del(uintptr_t a0, uintptr_t a1);
int32_t hdpp_user_module_init(void);
int32_t hdpp_user_module_exit(void);

/* ------------------------------------------------------------------ */
/* Sync layer: serialises host-side object updates against the device   */
/* register sync path (spin-wait + schedule_timeout).  Call graph shows */
/* every *_sync_write of band/device/sys/user/vap entering these locks. */
/* ------------------------------------------------------------------ */
int32_t hdpp_sync_read_lock(uintptr_t a0, uintptr_t a1);
int32_t hdpp_sync_read_unlock(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			      uintptr_t a3);
int32_t hdpp_sync_write_lock(uintptr_t a0);
int32_t hdpp_sync_write_unlock(uintptr_t a0);

/* ------------------------------------------------------------------ */
/* Device sync writers (push an object update to the chip)             */
/* ------------------------------------------------------------------ */
int32_t hdpp_band_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			     uintptr_t a3);
int32_t hdpp_device_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			       uintptr_t a3);
int32_t hdpp_sys_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2);
int32_t hdpp_user_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			     uintptr_t a3);
int32_t hdpp_vap_sync_read(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			   uintptr_t a3);
int32_t hdpp_vap_sync_write(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			    uintptr_t a3);

/* ------------------------------------------------------------------ */
/* Data-path hooks (called from hmac/wal/shuangta)                     */
/* ------------------------------------------------------------------ */

/* Per-band/vap/user TX MSDU counter/stat update on TX ring post. */
int32_t hdpp_tx_msdu_stat_update(uintptr_t a0, uintptr_t a1, uintptr_t a2,
				 uintptr_t a3);
/* RX MSDU stat update from the RX ring frame processing path. */
int32_t hdpp_rx_msdu_stat_update(uintptr_t a0, uintptr_t a1, uintptr_t a2,
				 uintptr_t a3);
/* Sequence-number bookkeeping for reorder/defrag, called from hmac TX
 * start_xmit and the shuangta RX path. */
int32_t hdpp_record_sequence_of_skb(uintptr_t a0, uintptr_t a1);
int32_t hdpp_record_rx_sequence_of_skb(uintptr_t a0, uintptr_t a1,
				       uintptr_t a2);
/* Packet-level diag hook on the TX/RX hot path (vendored "ping" logging). */
int32_t hdpp_ping_diag(uintptr_t a0, uintptr_t a1, uintptr_t a2);
/* Event forward to the device sync machinery (rekey, txq table address...). */
int32_t hdpp_config_send_event(uintptr_t a0, uintptr_t a1);
/* D2H sync handlers for per-user ERSRU / TWT updates.
 * TODO(probe): arity not yet measurable (no static call site). */
int32_t hdpp_d2h_user_ersru_syn(uintptr_t a0);
int32_t hdpp_d2h_user_twt_syn(uintptr_t a0);
/* RX netbuf enqueue from the rxdata thread.
 * TODO(probe): arity not yet measurable. */
int32_t hdpp_rxdata_netbuf_enqueue(uintptr_t a0);

/* ------------------------------------------------------------------ */
/* Stat core (counter model shared by hdpp and hmac)                   */
/* ------------------------------------------------------------------ */
int32_t hdpp_stat_inc(uintptr_t stat_id);
int32_t hdpp_stat_packets_inc(uintptr_t a0, uintptr_t a1, uintptr_t a2,
			      uintptr_t a3);
int32_t hdpp_stat_int64_add(uintptr_t a0, uintptr_t a1, uintptr_t a2);
int32_t hdpp_stat_int64_get(uintptr_t stat_id);
int32_t hdpp_stat_get(uintptr_t stat_id);

/* ------------------------------------------------------------------ */
/* Stat dfx exported API (46 functions, all in the vendor __ksymtab)   */
/* ------------------------------------------------------------------ */
/* These are debug/statistic control points registered into the vendor's
 * debug command framework through function-pointer tables (R_ARM_ABS32
 * relocations), which is why they have no static call sites in the binary
 * and their arity cannot be measured statically.  Signatures below follow
 * the family convention (switch/clear take an on/off or reset argument,
 * set_stat_num/set_interval/set_point_cnt/set_user_id take one numeric
 * argument, dump/detail/help take none) and are PROVISIONAL until the bench.
 * All return int32_t (0 = handled) as measured where call sites exist.
 */
#define HI5622_HDPP_DFX_PROVISIONAL

int32_t hdpp_stat_clear_hdpp(uintptr_t a0);
int32_t hdpp_stat_dump_all(uintptr_t a0);
int32_t hdpp_stat_dump_hdpp(uintptr_t a0);
int32_t hdpp_stat_dump_help(uintptr_t a0);
int32_t hdpp_stat_dump_item(uintptr_t a0);
int32_t hdpp_stat_dump_obj(uintptr_t a0);

/* rx irq queue length */
int32_t hdpp_stat_rx_irq_queue_len_clear(uintptr_t a0);
int32_t hdpp_stat_rx_irq_queue_len_dump(uintptr_t a0);
int32_t hdpp_stat_rx_irq_queue_len_set_stat_num(uintptr_t a0);
int32_t hdpp_stat_rx_irq_queue_len_switch(uintptr_t a0);

/* rx packets */
int32_t hdpp_stat_rx_packets_clear(uintptr_t a0);
int32_t hdpp_stat_rx_packets_dump(uintptr_t a0);
int32_t hdpp_stat_rx_packets_set_interval(uintptr_t a0);
int32_t hdpp_stat_rx_packets_switch(uintptr_t a0);

/* rx ping-pong queue length */
int32_t hdpp_stat_rx_ping_pong_queue_len_clear(uintptr_t a0);
int32_t hdpp_stat_rx_ping_pong_queue_len_dump(uintptr_t a0);
int32_t hdpp_stat_rx_ping_pong_queue_len_set_stat_num(uintptr_t a0);
int32_t hdpp_stat_rx_ping_pong_queue_len_switch(uintptr_t a0);

/* rx upstream processing time */
int32_t hdpp_stat_rx_upstream_proc_time_clear(uintptr_t a0);
int32_t hdpp_stat_rx_upstream_proc_time_dump(uintptr_t a0);
int32_t hdpp_stat_rx_upstream_proc_time_set_stat_num(uintptr_t a0);
int32_t hdpp_stat_rx_upstream_proc_time_switch(uintptr_t a0);

/* rx upstream queue length */
int32_t hdpp_stat_rx_upstream_queue_len_clear(uintptr_t a0);
int32_t hdpp_stat_rx_upstream_queue_len_dump(uintptr_t a0);
int32_t hdpp_stat_rx_upstream_queue_len_set_stat_num(uintptr_t a0);
int32_t hdpp_stat_rx_upstream_queue_len_switch(uintptr_t a0, uintptr_t a1);

/* rx wifi stay time (histogram of time-in-queue per direction) */
int32_t hdpp_stat_rx_wifi_stay_time_clear(uintptr_t a0);
int32_t hdpp_stat_rx_wifi_stay_time_dump(uintptr_t a0);
int32_t hdpp_stat_rx_wifi_stay_time_set_point_cnt(uintptr_t a0);
int32_t hdpp_stat_rx_wifi_stay_time_set_stat_num(uintptr_t a0, uintptr_t a1);
int32_t hdpp_stat_rx_wifi_stay_time_switch(uintptr_t a0, uintptr_t a1);

/* tx drop packets detail */
int32_t hdpp_stat_tx_drop_packets_detail(uintptr_t a0);

/* tx packets */
int32_t hdpp_stat_tx_packets_clear(uintptr_t a0);
int32_t hdpp_stat_tx_packets_dump(uintptr_t a0);
int32_t hdpp_stat_tx_packets_set_interval(uintptr_t a0);
int32_t hdpp_stat_tx_packets_switch(uintptr_t a0);

/* tx wifi depth (descriptor ring / tid buffer depth sampling) */
int32_t hdpp_stat_tx_wifi_depth_clear(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_depth_dump(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_depth_set_stat_num(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_depth_switch(uintptr_t a0);

/* tx wifi stay time */
int32_t hdpp_stat_tx_wifi_stay_time_clear(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_stay_time_dump(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_stay_time_set_point_cnt(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_stay_time_set_stat_num(uintptr_t a0, uintptr_t a1);
int32_t hdpp_stat_tx_wifi_stay_time_set_user_id(uintptr_t a0);
int32_t hdpp_stat_tx_wifi_stay_time_switch(uintptr_t a0, uintptr_t a1);

#endif /* HI5622_HDPP_H */
