/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dp_txrx.h - kernel glue for the first MAC data-plane TX/RX frames
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-dp-txrx, shared task task-122). The
 * portable device-free logic is dp_txrx.h/dp_txrx.c (host selftested); this
 * header only binds that logic to one PCI endpoint after the E1 data-plane
 * setup has succeeded (wifi-datapath-bringup dp_setup).
 *
 * Integration is a small patch to the wifi-datapath-bringup source tree:
 *   patches/0001-hi5622-dp-txrx.patch  (dp_setup bind + remove release +
 *   four sysfs attributes + Kbuild). tools/build.sh applies it to a scratch
 *   copy and compile-verifies against the project 6.18.60 tree.
 */
#ifndef _HI5622_DP_TXRX_H
#define _HI5622_DP_TXRX_H

#include <linux/device.h>
#include <linux/types.h>

struct hi5622_pci;

/* Snapshot of the E1 data-plane objects handed over by dp_setup. Addresses
 * are 32-bit device/host-window addresses (the driver's DMA window is
 * 0x80000000..0x8fffffff); 64-bit holders only avoid dma_addr_t width
 * differences on ARM32. */
struct hi5622_dp_txrx_bind {
	struct hi5622_pci *pci;

	void *tx_msdu_rings;           /* 8 rings x 64 x 8 B host DRAM */
	u64  tx_msdu_rings_dma;
	void *txq_table;               /* 0x2524 B host-DRAM TXQ table */
	u64  txq_table_dma;
	u32  txq_table_devva;
	void *txba;                    /* 0x800 B host-DRAM tx_ba_info buffer */
	u64  txba_dma;
	u32  txba_bytes;

	void *rx_norm_ring;            /* 256 x 4 B free ring */
	void *rx_small_ring;           /* 512 x 4 B free ring */
	void *rx_cmp_ring;             /* 4095 x 4 B completion ring */
	u64  rx_norm_ring_dma;
	u64  rx_small_ring_dma;
	u64  rx_cmp_ring_dma;
	void **rx_norm_cpu;            /* 256 coherent buffers (host word + DMA) */
	void **rx_small_cpu;           /* 512 coherent buffers */
	const u32 *rx_norm_dma;        /* coherent DMA addresses of the pools */
	const u32 *rx_small_dma;

	u32  msdu_table_ca;            /* device-RAM MSDU-info table base CA */
	u32  ready_payload;            /* ready g4 id2 payload (evidence) */
};

/* Called once at the end of a successful dp_setup: allocates the one TX frame
 * buffer and arms the tx/rx attribute surface for this endpoint. */
int  hi5622_dp_txrx_bind(struct hi5622_pci *ctx,
                         const struct hi5622_dp_txrx_bind *b);
/* Idempotent teardown (dp remove / failed bind). */
void hi5622_dp_txrx_release(struct hi5622_pci *ctx);

/* sysfs handlers; the patched hi5622_dp.c owns the device_attribute objects
 * (the 6.18 DEVICE_ATTR_RO/WO macros define them static, so they cannot be
 * shared across translation units). */
ssize_t hi5622_dp_tx_frame_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count);
ssize_t hi5622_dp_tx_status_show(struct device *dev,
                                 struct device_attribute *attr, char *buf);
ssize_t hi5622_dp_rx_poll_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count);
ssize_t hi5622_dp_rx_capture_show(struct device *dev,
                                  struct device_attribute *attr, char *buf);

/* wifi-rx-static (shared task task-137): fail-closed live-bank RX enable.
 * dp_rx_arm is read-only (validates + logs the live ring bank); dp_rx_drain
 * is the only action that moves RX ring pointers and only runs when the
 * rx_enable module parameter is 1. */
ssize_t hi5622_dp_rx_arm_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count);
ssize_t hi5622_dp_rx_drain_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count);

/* wifi-dp-tuning (shared task task-134): TXQ-table queue-state publish and the
 * bounded completion/consumer read surface. */
ssize_t hi5622_dp_txq_show(struct device *dev,
                           struct device_attribute *attr, char *buf);
ssize_t hi5622_dp_tx_complete_poll_store(struct device *dev,
                                         struct device_attribute *attr,
                                         const char *buf, size_t count);
ssize_t hi5622_dp_mac_ext_show(struct device *dev,
                               struct device_attribute *attr, char *buf);
ssize_t hi5622_dp_mac_tail_init_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count);

/* wifi-rx-linux-parity (shared task task-166): Linux RX-enable parity.
 * dp_rx_parity is READ-ONLY (live bank + MAC config block + the other machw
 * blocks, classified against the committed vendor stock capture).  dp_rx_kick
 * is the opt-in second exact-width producer publish.  dp_rx_ctl is the
 * one-shot, allowlisted, explicitly enabled RX-control candidate-word
 * experiment.  All are gated like the drain; the defaults refuse. */
ssize_t hi5622_dp_rx_parity_show(struct device *dev,
                                 struct device_attribute *attr, char *buf);
ssize_t hi5622_dp_rx_kick_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count);
ssize_t hi5622_dp_rx_ctl_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count);

#endif /* _HI5622_DP_TXRX_H */
