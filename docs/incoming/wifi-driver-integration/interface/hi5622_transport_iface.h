/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_transport_iface.h - host-side contract between the hi5622_pciet
 * PCIe/ETE transport module and a hdpp-style Wi-Fi driver.
 *
 * ORIGINAL text written by the wifi-driver-integration lane (task-64) from
 * the project's decoded interface contract; no vendor source text or object
 * code is reproduced.  Evidence markers follow the project convention:
 *   [V]  re-verified against a named local artifact,
 *   [V*] device-measured by an earlier attended lane and recorded in a doc,
 *   [I]  inference, [U] unknown / not yet pinned.
 *
 * Contract sources:
 *   phase37/THE-PROTOCOL-MAP.md  - boot dialogue, the 12-byte HCC header,
 *                                  the mailbox D2H service routine + ack/
 *                                  clear/re-arm order, H2D announce id 3,
 *                                  the H2D id-5 prohibition [V]
 *   wifi-driver-re/REPORT.md     - hcc_msg_process group/id dispatch, the
 *                                  16-byte handler records, the 8-byte
 *                                  per-id mailbox table [V]
 *   wifi-transport-port/REPORT.md- the transport skeleton this sits on [V]
 *   phase42/ete-registers-reconciled.md, phase43/device-dr-side.md
 *                                - ETE registers / device-side preconditions [V*]
 *
 * LAYERING
 *
 *   hi5622_pciet.ko                     hi5622_hdpp_drv.ko
 *   +-----------------------+           +---------------------------+
 *   | PCIe BAR0/BAR2, iATU  |           | hdpp/alg core, HCC layer  |
 *   | ETE SR0/DR0 rings     |<-- iface -| D2H dispatch ids 0..10    |
 *   | mailbox/doorbell      |  (this)   | netdev + wiphy stub       |
 *   | firmware upload/ready |           | TX/RX ETE ring service    |
 *   +-----------------------+           +---------------------------+
 *
 * The transport keeps ownership of the PCI device, the BAR mappings and every
 * device write; the driver never touches a BAR.  The driver binds by
 * hi5622_transport_register_client() and receives a transport handle plus an
 * ops table on attach(); it then registers its D2H dispatch table, supplies
 * an RX payload callback, performs the ETE ring service and consumes the
 * firmware-ready notifications through the ops table.
 *
 * ATTENDED-BENCH GATE (safety): the transport module parameter driver_mode
 * (default 0) controls whether the iface performs the phase37 1.2 D2H service
 * routine (ack / re-arm device writes) and dispatches to the driver.  With
 * driver_mode=0 the transport keeps its original write behaviour and an
 * attached driver only observes, which preserves the "probe writes nothing"
 * safety model.  Enabling it is an attended bench step (see REPORT.md).
 *
 * ATOMICITY CONTRACT (important):
 *   attach()/detach() run in process context and may sleep.
 *   fw_state(), rx() and every D2H handler registered through
 *   ops->d2h_register() may run from the transport poll thread or from an IRQ
 *   context (use_irq=1): they must not sleep and must not take mutexes that a
 *   process-context path can hold while calling back into the transport.
 *
 * The transport-side implementation of this interface is pciet_iface.c; the
 * marked delta hunks in the transport source are listed in
 * ../transport-delta/README.md and generated as
 * ../transport-delta/0001-pciet-integration-iface.patch.
 */
#ifndef _HI5622_TRANSPORT_IFACE_H
#define _HI5622_TRANSPORT_IFACE_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>

/* D2H mailbox-layer dispatch table size: the vendor's pcie_msg_init allocates
 * 11 entries * 8 B, pcie_msg_register rejects id > 10, and pcie_msg_handle
 * iterates the set bits of the D2H bitmap 0..10.  phase37 3.1 [V] */
#define HI5622_TR_D2H_MAX_ID	10
#define HI5622_TR_D2H_IDS	(HI5622_TR_D2H_MAX_ID + 1)

/* Transport state bits returned by ops->get_state() / passed to fw_state().
 * The transport is the authority for the first six; PLAT_READY/DEVICE_READY
 * are the HCC group-4 ready completes of phase37 3.2 [V] (the driver observes
 * them through its own group-4 handlers and may OR them in for waiters). */
#define HI5622_TR_STATE_PROBED		BIT(0)	/* PCI probe done, BARs mapped */
#define HI5622_TR_STATE_WINDOWS		BIT(1)	/* endpoint iATU readback OK */
#define HI5622_TR_STATE_FW_LOADED	BIT(2)	/* upload verified */
#define HI5622_TR_STATE_CPU_RELEASED	BIT(3)	/* 0x5a5a written */
#define HI5622_TR_STATE_GATE_ARMED	BIT(4)	/* 0xcece announce written */
#define HI5622_TR_STATE_PLAT_READY	BIT(5)	/* HCC group 4 id 1 complete */
#define HI5622_TR_STATE_DEVICE_READY	BIT(6)	/* HCC group 4 id 2 complete */
#define HI5622_TR_STATE_ALL		GENMASK(6, 0)

struct hi5622_transport;

/* D2H mailbox-layer handler: id is 0..HI5622_TR_D2H_MAX_ID (bit position in
 * the D2H bitmap at CA 0x40039014).  Called from atomic context. */
typedef void (*hi5622_tr_d2h_fn)(struct hi5622_transport *t, u32 id, void *arg);

/**
 * struct hi5622_transport_ops - everything the driver may do to the transport
 *
 * @name: transport instance name ("hi5622_pciet"), for logs
 * @dev: the endpoint's struct device (dev_name() usable), for dev_* logging
 * @id: enumeration index, 0 for the first probed endpoint (the board has two
 *      endpoints, one per RC); stable for the lifetime of the transport
 *
 * H2D path - all traffic is one SR0 node plus the mailbox announce id 3
 * (phase37 1.1 step 1 / 2.1 [V]).  The 12-byte HCC header and its payload
 * are built by the driver; the transport copies the message into the next
 * SR0 buffer, posts the 8-byte node {devva, (len<<16)|0x6d2b}, advances the
 * producer and writes the id-3 announce (pending mask 0x40039010, doorbell
 * drain-poll, doorbell write 0x400392d4).
 *
 * @h2d_send: post one complete HCC frame (12-byte header + payload) and
 *            announce id 3; %0 or a negative errno.  May be called from the
 *            driver's TX workqueue context; the transport serialises access.
 * @h2d_kick: raw mailbox announce, boot steps only.  Only id 3 is accepted:
 *            the device-side H2D table registers ids {1,3,5,6} and id 5 is a
 *            BUG() trap on this firmware, so the driver MUST NOT post id 5
 *            (phase37 1.1 step 3 [V]).  Use h2d_send for all data-path H2D.
 *
 * D2H path - the driver registers handlers for the mailbox ids it services;
 * the vendor host registers exactly {1,3,6,7} (phase37 3.1 [V]) and replies
 * to none of them.  The transport performs the fixed service routine
 * (ack CA 0x40101438 = 1, read+drain the bitmap, re-arm CA 0x40101414 = 1,
 * phase37 1.2 [V]) and then dispatches the set bits lowest-first: the
 * transport's own boot handlers first, then the driver's handler for the id.
 *
 * @d2h_register: register a handler for one mailbox id; -EINVAL if id > 10,
 *                -ENOTCONN before attach, -EBUSY if a driver handler is
 *                already registered for that id.
 * @d2h_unregister: drop the driver handler for one id (no-op if none).
 *
 * ETE ring service:
 * @ete_init: program SR0 (group A) + DR0 (group B) channel registers with the
 *            ring buffers the transport allocated at probe.  This performs
 *            DEVICE writes and is bench-gated: the transport returns -EPERM
 *            unless the attended-write gate is open (phase37 1.1 + transport
 *            BENCH-PLAN stage B2).  The vendor channel +0x00 enable is set by
 *            the firmware, not the host (hcc-re 4.2 [V]); the transport leaves
 *            it untouched.  NOTE: B3 needs all SR0-2 + DR0-3 channels armed
 *            (hcc-re 9 P0); this op currently covers the control pair only -
 *            see ../transport-delta/README.md "Required transport changes".
 * @ete_tx: post one SR0 node (used by h2d_send; exposed separately so the
 *          bring-up can post a node without a busy-wait on the HCC queue).
 * @ete_rx_service: drain DR0 completed nodes and deliver each payload to the
 *          client rx() callback; returns the number of payloads delivered.
 * @ete_depth / @ete_buf_len: ring geometry (depth 32, per-node buffer size).
 *
 * Firmware-ready / state:
 * @get_state: snapshot of the HI5622_TR_STATE_* bits.
 * @wait_ready: sleep until (state & mask) == mask, or timeout; %0 on success,
 *              -ETIMEDOUT, or -ERESTARTSYS.  Process context only.
 * @d2h_count: monotonic count of serviced D2H bitmap events (diagnostics).
 *
 * All function pointers are non-NULL after a successful attach().
 */
struct hi5622_transport_ops {
	const char *name;
	struct device *dev;
	u32 id;

	int  (*h2d_send)(struct hi5622_transport *t, const void *msg, u32 len);
	int  (*h2d_kick)(struct hi5622_transport *t, u32 mailbox_id);

	int  (*d2h_register)(struct hi5622_transport *t, u32 id,
			     hi5622_tr_d2h_fn fn, void *arg);
	void (*d2h_unregister)(struct hi5622_transport *t, u32 id);

	int  (*ete_init)(struct hi5622_transport *t);
	int  (*ete_tx)(struct hi5622_transport *t, const void *data, u32 len);
	int  (*ete_rx_service)(struct hi5622_transport *t);
	u32  (*ete_depth)(struct hi5622_transport *t);
	u32  (*ete_buf_len)(struct hi5622_transport *t);

	u32  (*get_state)(struct hi5622_transport *t);
	int  (*wait_ready)(struct hi5622_transport *t, u32 state_mask,
			   u32 timeout_ms);
	u32  (*d2h_count)(struct hi5622_transport *t);
};

/**
 * struct hi5622_transport_client - a hdpp-style driver binding
 *
 * @name: driver name, for logs
 * @owner: THIS_MODULE of the client; the iface holds a reference while a
 *         callback is in flight, so the client module cannot unload under it
 * @priv: driver-private pointer passed back to every callback
 * @attach: process context; the transport is ready to be used.  Register the
 *          D2H table and the rx() callback here, then schedule work; do not
 *          block on device events from attach().
 * @detach: process context; the transport is going away (or the client
 *          unregistered).  No callback can be in flight when it starts, but
 *          callbacks that were queued by the driver itself are the driver's
 *          responsibility.  The transport may still be alive for other users.
 * @fw_state: atomic context; called whenever the transport state bits change
 *          (and once on attach with the current state).  Report the mask and
 *          the D2H id that caused the change (0xff for an attach-time call).
 * @rx: atomic context; one completed D2H HCC frame (12-byte header +
 *      payload).  The buffer is only valid for the duration of the call.
 */
struct hi5622_transport_client {
	const char *name;
	struct module *owner;
	void *priv;

	void (*attach)(struct hi5622_transport *t,
		       const struct hi5622_transport_ops *ops, void *priv);
	void (*detach)(struct hi5622_transport *t, void *priv);
	void (*fw_state)(struct hi5622_transport *t, u32 state, u32 d2h_id,
			 void *priv);
	void (*rx)(struct hi5622_transport *t, const void *buf, u32 len,
		   void *priv);
};

/* Driver -> transport: bind/unbind.  register_client() attaches the client to
 * every transport instance that is already probed and to every endpoint that
 * probes later; unregister_client() detaches it everywhere and waits for
 * in-flight callbacks.  Both take process context and block. */
int  hi5622_transport_register_client(struct hi5622_transport_client *client);
void hi5622_transport_unregister_client(struct hi5622_transport_client *client);

/**
 * hi5622_tr_state_str() - human-readable state mask, e.g. "probed|fw" 
 *
 * Writes a short static-buffer-free description into @buf; returns @buf.
 * Safe in any context.
 */
const char *hi5622_tr_state_str(u32 state, char *buf, size_t len);

/* ------------------------------------------------------------------------
 * Transport-side hooks (called from the marked delta hunks in pciet_main.c;
 * not part of the driver API).  They are exported only inside the transport
 * module - the driver module never calls them.
 * ---------------------------------------------------------------------- */
struct hi5622_pci;

/* D2H service: returns true when a client is attached and iface has
 * performed the whole phase37 service routine for this scan (the legacy
 * transport path must then be skipped). */
bool pciet_iface_d2h_client_scan(struct hi5622_pci *ctx);

/* RX payload fan-out from the transport's DR-ring consumer.  Returns true
 * when the payload was delivered to a client (the transport's own logging is
 * skipped), false when no client is attached. */
bool pciet_iface_deliver_rx(struct hi5622_pci *ctx, const void *buf, u32 len);

/* Instance lifecycle from pciet_probe()/pciet_remove(). */
void pciet_iface_transport_added(struct hi5622_pci *ctx);
void pciet_iface_transport_removed(struct hi5622_pci *ctx);

#endif /* _HI5622_TRANSPORT_IFACE_H */
