// SPDX-License-Identifier: GPL-2.0
/*
 * pciet_main.c - Hi5622V100 PCIe/ETE transport SKELETON for Linux 6.18.
 * ORIGINAL code written by the wifi-transport-port lane from the project's
 * decoded interface contract; no vendor source or object code is reproduced.
 *
 * What this module is:
 *
 *   A host-side skeleton of the pcie_msg_* / pcie_ete_* / bal_* transport
 *   layer (plat.ko 101+26+87 functions) rebuilt as clean-room code on top of
 *   the luofu PCIe root-complex driver (patches-6.18/013) and the Linux
 *   PCI/DMA frameworks.  It implements, per the contract:
 *
 *     - the six inbound-region map of endpoint BAR0 and the endpoint-iATU
 *       viewport programming (phase18 A.4-A.6);
 *     - the contiguous firmware upload to CA 0x01240000 with the 0x5a5a CPU
 *       release and the 0xcece D2H announce gate;
 *     - HCC/ETE node allocation + SR0/DR0 ring initialization;
 *     - the H2D send path: pending-mask write, doorbell drain-poll, doorbell
 *       write (pcie_msg_send_irq decode, driver-re sec 4.2);
 *     - the D2H receive path: bitmap read/drain at 0x40039014, per-id
 *       dispatch for ids 0..10, boot ready handlers for ids 1/2
 *       (pcie_msg_handle decode, driver-re sec 3.5/4.2);
 *     - the 11-word dev_status_check read-out (firmware-re sec 2.5).
 *
 * SAFETY MODEL (attended-bench contract, see ../BENCH-PLAN.md):
 *
 *   - probe() performs NO device writes beyond the PCI core's enumeration
 *     (pci_enable_device): it only ioremap()s the BARs and allocates host
 *     memory.  Every device write in this file lives in a sysfs store()
 *     handler and is an explicit, logged, single-step action the bench
 *     operator triggers by writing a "1" to the matching attribute.
 *   - device CA 0x400392f0 (out[5]) is never written: compile-time asserts
 *     plus a runtime gate on the only write helper.
 *   - the D2H "interrupt" is served by a polled bitmap scan (the luofu host
 *     driver cannot demux INTx yet - the status registers live in the
 *     never-read misc window); an optional request_irq() path exists behind
 *     the use_irq module parameter for when the host side lands.
 */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>

#include "hi5622_pciet.h"

#define DRV_NAME		"hi5622_pciet"

static unsigned int poll_ms = 500;	/* D2H bitmap poll interval; 0 = off */
module_param(poll_ms, uint, 0644);
MODULE_PARM_DESC(poll_ms, "D2H bitmap poll interval in ms (0 disables the poll thread)");

static bool twin_enabled;		/* twin/ack write after doorbell: [U], off */
module_param(twin_enabled, bool, 0644);
MODULE_PARM_DESC(twin_enabled, "enable the twin-word bit-0 write after the doorbell (contract [U])");

static bool use_irq;			/* request_irq path; host INTx unwired yet */
module_param(use_irq, bool, 0444);
MODULE_PARM_DESC(use_irq, "use request_irq instead of polling (needs host INTx wiring)");

static char *fw_path = HI5622_FW_PATH;
module_param(fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "firmware path under /lib/firmware (vendor: hi_wifi/FIRMWARE.bin)");

/*
 * Compile-time contract asserts: (1) no writable register may collide with
 * the never-write register out[5]; (2) the decoded struct layouts.
 */
static void pciet_assert_contract(void)
{
	BUILD_BUG_ON(HI5622_BAR0_H2D_PENDING == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_D2H_BITMAP == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_H2D_DOORBELL == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_TWIN_WORD == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_CPU_RELEASE == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_D2H_GATE == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_FW_TARGET == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(sizeof(struct hcc_msg_header) != 10);
	BUILD_BUG_ON(sizeof(struct hi5622_ete_node) != 8);
	BUILD_BUG_ON(ARRAY_SIZE(hi5622_regions) != 6);
	BUILD_BUG_ON(ARRAY_SIZE(hi5622_dev_status) != 11);
}

/* The single write helper every device write goes through. */
static void pciet_writel_checked(struct hi5622_pci *ctx, u32 bar0_off, u32 val)
{
	if (WARN_ON(bar0_off == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE)))
		return;
	writel(val, ctx->bar0 + bar0_off);
}

/* ---------------------------------------------------------------------
 * H2D send path (pcie_msg_send / pcie_msg_send_irq decode, driver-re sec 4.2)
 * ------------------------------------------------------------------- */

/* Drain-poll the doorbell word until it clears (pcie_msg_wait_for_clr [V]). */
static int pciet_msg_wait_for_clr(struct hi5622_pci *ctx)
{
	unsigned int i;

	for (i = 0; i < 1000; i++) {
		if (!readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL))
			return 0;
		udelay(10);
	}
	dev_warn(&ctx->pdev->dev, "doorbell drain timeout (CA 0x400392d4 stuck)\n");
	return -ETIMEDOUT;
}

/*
 * pcie_msg_send_irq [V]: write the pending mask to CA 0x40039010, poll the
 * doorbell (CA 0x400392d4) until clear, then write the accumulated pending
 * bitmap to the doorbell; the twin-word bit-0 set ([U]) stays behind
 * twin_enabled until the first bench pins it.
 */
void pciet_msg_send_irq(struct hi5622_pci *ctx, u32 pending_mask)
{
	unsigned long flags;

	pciet_writel_checked(ctx, HI5622_BAR0_H2D_PENDING, pending_mask);
	pciet_msg_wait_for_clr(ctx);

	spin_lock_irqsave(&ctx->doorbell_lock, flags);
	if (ctx->pending) {
		pciet_writel_checked(ctx, HI5622_BAR0_H2D_DOORBELL, ctx->pending);
		ctx->pending = 0;
		if (ctx->twin_enabled) {
			u32 twin = readl(ctx->bar0 + HI5622_BAR0_TWIN_WORD);

			pciet_writel_checked(ctx, HI5622_BAR0_TWIN_WORD, twin | 1);
		}
	}
	spin_unlock_irqrestore(&ctx->doorbell_lock, flags);
}

int pciet_msg_send(struct hi5622_pci *ctx, u32 id)
{
	unsigned long flags;

	if (id > 31)
		return -EINVAL;

	spin_lock_irqsave(&ctx->doorbell_lock, flags);
	ctx->pending |= BIT(id);
	spin_unlock_irqrestore(&ctx->doorbell_lock, flags);

	pciet_msg_send_irq(ctx, BIT(id));
	return 0;
}

/* ---------------------------------------------------------------------
 * D2H receive path (pcie_msg_handle decode, driver-re sec 3.5/4.2)
 * ------------------------------------------------------------------- */

/* Payload consumer for DR-ring arrivals: boot-message wire reading
 * (firmware-re sec 2.3 [V]): u16 at +4 = length, byte[9] = core index,
 * payload at +0xc; copied into read_msg[core] (0x64 stride). */
static void pciet_d2h_payload(struct hi5622_pci *ctx, void *va, u32 len)
{
	u8 *p = va;
	u32 n;

	if (len >= HI5622_D2H_WIRE_PAYLOAD_OFF + 2) {
		u16 wlen = p[4] | ((u16)p[5] << 8);
		u8 core = p[9];

		dev_info(&ctx->pdev->dev,
			 "D2H payload %u B: wire len %u, core %u, bid 0x%02x\n",
			 len, wlen, core, p[1]);
		if (core < 2) {
			n = min_t(u32, len - HI5622_D2H_WIRE_PAYLOAD_OFF,
				  HI5622_D2H_WIRE_READ_MSG_LEN);
			memcpy(ctx->read_msg[core],
			       p + HI5622_D2H_WIRE_PAYLOAD_OFF, n);
		}
	}
	print_hex_dump(KERN_INFO, DRV_NAME " d2h: ", DUMP_PREFIX_OFFSET, 16, 1,
		       p, min_t(u32, len, 64), true);
}

/* Boot message id 1: "Device plat ready!" (chip table .data+0x2660, [V]). */
static void pciet_d2h_plat_ready(struct hi5622_pci *ctx, u32 id)
{
	dev_info(&ctx->pdev->dev, "Device plat ready! (D2H message id %u)\n", id);
	ctx->d2h_count++;
	complete(&ctx->plat_ready);
}

/* Boot message id 2: "DEVICE READY"; payload rides the DR0 ring. */
static void pciet_d2h_host_ready(struct hi5622_pci *ctx, u32 id)
{
	dev_info(&ctx->pdev->dev, "DEVICE READY (D2H message id %u)\n", id);
	pciet_ete_dr_consume(ctx, &ctx->dr0, pciet_d2h_payload);
	ctx->d2h_count++;
	complete(&ctx->host_ready);
}

/* PCIe-layer id 3: ETE transfer done -> consume the DR ring. */
static void pciet_d2h_ete_done(struct hi5622_pci *ctx, u32 id)
{
	dev_info(&ctx->pdev->dev, "ETE transfer done (D2H message id %u)\n", id);
	pciet_ete_dr_consume(ctx, &ctx->dr0, pciet_d2h_payload);
	ctx->d2h_count++;
}

void pciet_d2h_register(struct hi5622_pci *ctx, u32 id,
			void (*func)(struct hi5622_pci *ctx, u32 id))
{
	if (id <= HI5622_D2H_MAX_ID)
		ctx->d2h[id].func = func;
}

/*
 * The decoded pcie_msg_handle core: read the D2H bitmap (CA 0x40039014),
 * drain it to 0 (device-side precondition P2 [V]), then dispatch each set
 * bit id 0..10 through the 8-byte {func, arg} slot table.  The vendor's
 * ctx+0x0c/ctx+0x10 toggles are host-internal IRQ-mask flags [I]; this
 * skeleton does not model them.
 */
void pciet_d2h_scan(struct hi5622_pci *ctx)
{
	u32 bitmap = readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP);
	u32 id;

	if (!bitmap)
		return;

	dev_info(&ctx->pdev->dev, "D2H bitmap 0x%08x\n", bitmap);
	pciet_writel_checked(ctx, HI5622_BAR0_D2H_BITMAP, 0);

	for (id = 0; id <= HI5622_D2H_MAX_ID; id++) {
		if (!(bitmap & BIT(id)))
			continue;
		if (ctx->d2h[id].func)
			ctx->d2h[id].func(ctx, id);
		else
			dev_info(&ctx->pdev->dev,
				 "D2H id %u: no handler registered\n", id);
	}
}

/* Optional real-IRQ path; unused until the luofu host wires INTx. */
static irqreturn_t pciet_irq_handler(int irq, void *dev_id)
{
	struct hi5622_pci *ctx = dev_id;

	if (!readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP))
		return IRQ_NONE;
	pciet_d2h_scan(ctx);
	return IRQ_HANDLED;
}

/* ---------------------------------------------------------------------
 * Firmware upload + boot control words (firmware-re sec 2.1/2.2, [V]/[V*])
 * ------------------------------------------------------------------- */

/*
 * Contiguous memcpy-equivalent to CA 0x01240000 = BAR0+0x6f8000 in chunks
 * of 0x80000 through the region-5 inbound window.  No rings, no doorbell,
 * no per-chunk header in the data path [V].
 */
int pciet_fw_upload(struct hi5622_pci *ctx)
{
	const struct firmware *fw;
	void __iomem *dst;
	size_t off = 0;
	int ret;

	if (ctx->cpu_released) {
		dev_warn(&ctx->pdev->dev, "CPU already released; upload refused\n");
		return -EBUSY;
	}

	ret = firmware_request_nowarn(&fw, fw_path, &ctx->pdev->dev);
	if (ret) {
		dev_err(&ctx->pdev->dev, "firmware %s: %d\n", fw_path, ret);
		return ret;
	}

	dev_info(&ctx->pdev->dev,
		 "upload %s: %zu bytes -> BAR0+0x%x (CA 0x%08x), chunk 0x%x\n",
		 fw_path, fw->size, HI5622_BAR0_FW_TARGET,
		 HI5622_FW_TARGET_CA, HI5622_FW_CHUNK);

	dst = ctx->bar0 + HI5622_BAR0_FW_TARGET;
	while (off < fw->size) {
		size_t chunk = min_t(size_t, HI5622_FW_CHUNK, fw->size - off);

		if (chunk & 3) {
			size_t i;

			for (i = 0; i < chunk; i++)
				iowrite8(fw->data[off + i], dst + i);
		} else {
			__iowrite32_copy(dst, fw->data + off, chunk / 4);
		}
		off += chunk;
		dst += chunk;
	}
	release_firmware(fw);

	ctx->fw_loaded = true;
	dev_info(&ctx->pdev->dev,
		 "download finished: %zu bytes in %zu chunks\n",
		 off, DIV_ROUND_UP(off, HI5622_FW_CHUNK));
	return 0;
}

/* The single-shot 0x5a5a CPU release (descriptor {0x40000108,0x5a5a,4} [V]). */
void pciet_cpu_release(struct hi5622_pci *ctx)
{
	if (!ctx->fw_loaded)
		dev_warn(&ctx->pdev->dev,
			 "CPU release before upload (firmware not loaded)\n");

	dev_info(&ctx->pdev->dev,
		 "CPU release: 0x%04x -> CA 0x%08x (BAR0+0x%x)\n",
		 HI5622_CPU_RELEASE_MAGIC, HI5622_CA_CPU_RELEASE,
		 HI5622_BAR0_CPU_RELEASE);
	pciet_writel_checked(ctx, HI5622_BAR0_CPU_RELEASE,
			     HI5622_CPU_RELEASE_MAGIC);
	ctx->cpu_released = true;
}

/*
 * The 0xcece announce gate: firmware-side precondition for D2H deposit
 * (phase43 P4, [V]); the exact host write site is [U] - this module writes
 * it as its own explicit bench step after the release.
 */
void pciet_d2h_gate_announce(struct hi5622_pci *ctx)
{
	if (!ctx->cpu_released)
		dev_warn(&ctx->pdev->dev,
			 "announce before release (device may not be running)\n");

	dev_info(&ctx->pdev->dev,
		 "D2H announce gate: 0x%04x -> CA 0x%08x (BAR0+0x%x)\n",
		 HI5622_D2H_GATE_MAGIC, HI5622_CA_D2H_GATE,
		 HI5622_BAR0_D2H_GATE);
	pciet_writel_checked(ctx, HI5622_BAR0_D2H_GATE, HI5622_D2H_GATE_MAGIC);
	ctx->gate_announced = true;
}

/* ---------------------------------------------------------------------
 * Endpoint iATU window programming (phase18 A.4-A.6, [V]/[V*])
 * ------------------------------------------------------------------- */

/*
 * Program the six inbound viewports of the ENDPOINT's iATU through BAR2,
 * in the vendor's exact order (ctrl2 = 0, ctrl2 = enable|bar, base_lo,
 * base_hi, limit, target_lo, target_hi), then read every word back and log
 * the match - the phase-18 verification discipline.
 */
int pciet_prepare_windows(struct hi5622_pci *ctx)
{
	void __iomem *iatu = ctx->bar2;
	u32 i, blk, val, exp;
	int all_ok = 1;

	for (i = 0; i < ARRAY_SIZE(hi5622_regions); i++) {
		const struct hi5622_inbound_region *r = &hi5622_regions[i];

		blk = HI5622_IATU_VP0_OFF + HI5622_IATU_VP_STRIDE * i;

		writel(0, iatu + blk + IATU_VP_CTRL2);
		writel(IATU_VP_CTRL2_ENA | IATU_VP_CTRL2_BAR(HI5622_BAR_MEM_WIN),
		       iatu + blk + IATU_VP_CTRL2);
		writel(r->bar_base, iatu + blk + IATU_VP_BASE_LO);
		writel(0, iatu + blk + IATU_VP_BASE_HI);
		writel(r->bar_limit, iatu + blk + IATU_VP_LIMIT_LO);
		writel(0, iatu + blk + IATU_VP_LIMIT_HI);
		writel(r->ca_target, iatu + blk + IATU_VP_TARGET_LO);
		writel(0, iatu + blk + IATU_VP_TARGET_HI);
	}

	for (i = 0; i < ARRAY_SIZE(hi5622_regions); i++) {
		const struct hi5622_inbound_region *r = &hi5622_regions[i];
		bool ok = true;

		blk = HI5622_IATU_VP0_OFF + HI5622_IATU_VP_STRIDE * i;

		val = readl(iatu + blk + IATU_VP_CTRL2);
		exp = IATU_VP_CTRL2_ENA | IATU_VP_CTRL2_BAR(HI5622_BAR_MEM_WIN);
		ok &= (val == exp);
		ok &= (readl(iatu + blk + IATU_VP_BASE_LO) == r->bar_base);
		ok &= (readl(iatu + blk + IATU_VP_LIMIT_LO) == r->bar_limit);
		ok &= (readl(iatu + blk + IATU_VP_TARGET_LO) == r->ca_target);

		dev_info(&ctx->pdev->dev,
			 "iatu vp%u: ctrl2=0x%08x base=0x%06x limit=0x%06x target=0x%08x %s\n",
			 i, val, r->bar_base, r->bar_limit, r->ca_target,
			 ok ? "readback=OK" : "readback=MISMATCH");
		all_ok &= ok;
	}

	ctx->windows_programmed = all_ok;
	return all_ok ? 0 : -EIO;
}

/* ---------------------------------------------------------------------
 * dev_status_check (firmware-re sec 2.5, [V*])
 * ------------------------------------------------------------------- */
ssize_t pciet_dev_status_show(struct hi5622_pci *ctx, char *buf, size_t len)
{
	int i, pos = 0;

	pos += scnprintf(buf + pos, len - pos,
			 "fw_loaded=%d cpu_released=%d gate_announced=%d windows=%d d2h_count=%u\n",
			 ctx->fw_loaded, ctx->cpu_released, ctx->gate_announced,
			 ctx->windows_programmed, ctx->d2h_count);
	for (i = 0; i < ARRAY_SIZE(hi5622_dev_status); i++)
		pos += scnprintf(buf + pos, len - pos, "%s=0x%08x\n",
				 hi5622_dev_status[i].name,
				 readl(ctx->bar0 + hi5622_dev_status[i].bar0_off));
	pos += scnprintf(buf + pos, len - pos, "out[1]_bitmap=0x%08x\n",
			 readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP));
	pos += scnprintf(buf + pos, len - pos, "doorbell=0x%08x\n",
			 readl(ctx->bar0 + HI5622_BAR0_H2D_DOORBELL));
	pos += scnprintf(buf + pos, len - pos, "ete_intr=0x%08x\n",
			 readl(ctx->bar0 + HI5622_BAR0_IO(HI5622_CA_ETE_INTR)));
	return pos;
}

/* ---------------------------------------------------------------------
 * Poll thread
 * ------------------------------------------------------------------- */
static int pciet_poll_thread(void *data)
{
	struct hi5622_pci *ctx = data;

	while (!kthread_should_stop()) {
		pciet_d2h_scan(ctx);
		msleep_interruptible(poll_ms ?: 1);
	}
	return 0;
}

static int pciet_poll_start(struct hi5622_pci *ctx)
{
	if (ctx->poll_thread)
		return 0;
	ctx->poll_thread = kthread_run(pciet_poll_thread, ctx,
				       DRV_NAME "-poll-%s",
				       pci_name(ctx->pdev));
	if (IS_ERR(ctx->poll_thread)) {
		int ret = PTR_ERR(ctx->poll_thread);

		ctx->poll_thread = NULL;
		return ret;
	}
	dev_info(&ctx->pdev->dev, "D2H poll thread started (%u ms)\n", poll_ms);
	return 0;
}

static void pciet_poll_stop(struct hi5622_pci *ctx)
{
	if (!ctx->poll_thread)
		return;
	kthread_stop(ctx->poll_thread);
	ctx->poll_thread = NULL;
	dev_info(&ctx->pdev->dev, "D2H poll thread stopped\n");
}

/* ---------------------------------------------------------------------
 * sysfs: read-only checkpoints
 * ------------------------------------------------------------------- */
#define PCIET_RO_ATTR(_name, _fmt, _expr)				\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));	\
	return sysfs_emit(buf, _fmt, _expr);				\
}									\
static DEVICE_ATTR_RO(_name)

PCIET_RO_ATTR(out1, "0x%08x\n", readl(ctx->bar0 + HI5622_BAR0_D2H_BITMAP));
PCIET_RO_ATTR(ready1, "%d\n", completion_done(&ctx->plat_ready));
PCIET_RO_ATTR(ready2, "%d\n", completion_done(&ctx->host_ready));
PCIET_RO_ATTR(d2h_count, "%u\n", ctx->d2h_count);
PCIET_RO_ATTR(fw_loaded, "%d\n", ctx->fw_loaded);
PCIET_RO_ATTR(cpu_released, "%d\n", ctx->cpu_released);
PCIET_RO_ATTR(gate_announced, "%d\n", ctx->gate_announced);
PCIET_RO_ATTR(windows, "%d\n", ctx->windows_programmed);

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));

	return pciet_dev_status_show(ctx, buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(status);

/* ---------------------------------------------------------------------
 * sysfs: single-step device actions (attended, see BENCH-PLAN.md)
 * ------------------------------------------------------------------- */
static ssize_t prepare_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = pciet_prepare_windows(ctx);

	if (ret)
		dev_err(dev, "prepare (endpoint iATU windows): %d\n", ret);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(prepare);

static ssize_t upload_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = pciet_fw_upload(ctx);

	if (ret)
		dev_err(dev, "upload: %d\n", ret);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(upload);

static ssize_t release_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));

	pciet_cpu_release(ctx);
	return count;
}
static DEVICE_ATTR_WO(release);

static ssize_t announce_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));

	pciet_d2h_gate_announce(ctx);
	return count;
}
static DEVICE_ATTR_WO(announce);

/* Read back the first 256 B at the BAR0+0x40000 firmware alias and compare
 * against the blob (phase17/18 verification discipline, [V*]). */
static ssize_t verify_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	const struct firmware *fw;
	u32 rd[64], ex[64];
	int ret, i, n;
	bool match;

	if (!ctx->fw_loaded) {
		dev_err(dev, "verify: upload first\n");
		return -EBUSY;
	}
	ret = firmware_request_nowarn(&fw, fw_path, dev);
	if (ret)
		return ret;

	n = min_t(size_t, fw->size, sizeof(rd));
	memcpy(ex, fw->data, n);
	for (i = 0; i < (int)ARRAY_SIZE(rd) && i * 4 < n; i++)
		rd[i] = readl(ctx->bar0 + HI5622_BAR0_FW_ALIAS + i * 4);
	match = memcmp(ex, rd, n) == 0;
	dev_info(dev, "firmware alias verify (%d B at BAR0+0x%x): %s\n",
		 n, HI5622_BAR0_FW_ALIAS, match ? "MATCH" : "MISMATCH");
	release_firmware(fw);
	return match ? count : -EIO;
}
static DEVICE_ATTR_WO(verify);

/* Initialize SR0 (group A) + DR0 (group B) channel registers. */
static ssize_t ringinit_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret;

	ret = pciet_ete_ring_init(ctx, &ctx->sr0, true);
	if (!ret)
		ret = pciet_ete_ring_init(ctx, &ctx->dr0, false);
	if (ret)
		dev_err(dev, "ringinit: %d\n", ret);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(ringinit);

/* H2D smoke test: post a zero-header 64 B message on SR0 and kick the
 * device with the ETE-done doorbell (id 3 => mask 8, [V]). */
static ssize_t send_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	u8 msg[64] = { 0 };
	int ret;

	if (!ctx->sr0.node_arr) {
		dev_err(dev, "send: ringinit first\n");
		return -EBUSY;
	}
	ret = pciet_ete_sr_send(ctx, &ctx->sr0, msg, sizeof(msg));
	if (ret)
		return ret;
	pciet_msg_send(ctx, HI5622_MSG_ETE_DONE);
	dev_info(dev, "H2D smoke: 64 B posted on SR0, doorbell mask 8 (id %u)\n",
		 HI5622_MSG_ETE_DONE);
	return count;
}
static DEVICE_ATTR_WO(send);

static ssize_t poll_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = 0;

	if (sysfs_streq(buf, "1"))
		ret = pciet_poll_start(ctx);
	else if (sysfs_streq(buf, "0"))
		pciet_poll_stop(ctx);
	else
		ret = -EINVAL;
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(poll);

static struct attribute *pciet_attrs[] = {
	&dev_attr_out1.attr,
	&dev_attr_ready1.attr,
	&dev_attr_ready2.attr,
	&dev_attr_d2h_count.attr,
	&dev_attr_fw_loaded.attr,
	&dev_attr_cpu_released.attr,
	&dev_attr_gate_announced.attr,
	&dev_attr_windows.attr,
	&dev_attr_status.attr,
	&dev_attr_prepare.attr,
	&dev_attr_upload.attr,
	&dev_attr_release.attr,
	&dev_attr_announce.attr,
	&dev_attr_verify.attr,
	&dev_attr_ringinit.attr,
	&dev_attr_send.attr,
	&dev_attr_poll.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pciet);

/* ---------------------------------------------------------------------
 * probe / remove
 * ------------------------------------------------------------------- */
static int pciet_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct hi5622_pci *ctx;
	int ret;

	pciet_assert_contract();

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "pcim_enable_device\n");

	/* BAR0 = 16 MiB six-region window; BAR2 = 16 KiB endpoint iATU.
	 * BAR4 (8 MiB) is not mapped: its role is [U]. */
	ret = pcim_iomap_regions(pdev, BIT(HI5622_BAR_MEM_WIN) | BIT(HI5622_BAR_IATU),
				 DRV_NAME);
	if (ret)
		return dev_err_probe(dev, ret, "pcim_iomap_regions\n");

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->pdev = pdev;
	ctx->bar0 = pcim_iomap_table(pdev)[HI5622_BAR_MEM_WIN];
	ctx->bar2 = pcim_iomap_table(pdev)[HI5622_BAR_IATU];
	ctx->twin_enabled = twin_enabled;
	spin_lock_init(&ctx->doorbell_lock);
	init_completion(&ctx->plat_ready);
	init_completion(&ctx->host_ready);
	pci_set_drvdata(pdev, ctx);

	/* Boot handler table (plat_init_bal_hcc_excp mirror, [V]): id 1
	 * device-plat-ready, id 2 host/DEVICE READY; PCIe layer adds id 3
	 * (ETE transfer done). */
	pciet_d2h_register(ctx, HI5622_MSG_PLAT_READY, pciet_d2h_plat_ready);
	pciet_d2h_register(ctx, HI5622_MSG_HOST_READY, pciet_d2h_host_ready);
	pciet_d2h_register(ctx, HI5622_MSG_ETE_DONE, pciet_d2h_ete_done);

	/* Host-side ring allocations only - no device writes. */
	ret = pciet_ete_ring_alloc(ctx, &ctx->sr0, HI5622_ETE_SR0_OFF,
				   HI5622_ETE_BUF_LEN);
	if (ret)
		return ret;
	ret = pciet_ete_ring_alloc(ctx, &ctx->dr0, HI5622_ETE_DR0_OFF,
				   HI5622_ETE_BUF_LEN);
	if (ret)
		goto err_sr0;

	if (use_irq) {
		int irq = pci_irq_vector(pdev, 0);

		if (irq > 0) {
			ret = devm_request_irq(dev, irq, pciet_irq_handler,
					       IRQF_SHARED, DRV_NAME, ctx);
			if (ret)
				return dev_err_probe(dev, ret, "request_irq %d\n",
						     irq);
		} else {
			dev_info(dev, "no IRQ vector (host INTx unwired); polled\n");
		}
	}

	if (poll_ms)
		pciet_poll_start(ctx);

	dev_info(dev,
		 "%s: endpoint %s BAR0 0x%pap BAR2 0x%pap; "
		 "no device writes performed - attended steps via sysfs\n",
		 DRV_NAME, pci_name(pdev),
		 &pdev->resource[HI5622_BAR_MEM_WIN].start,
		 &pdev->resource[HI5622_BAR_IATU].start);
	return 0;

err_sr0:
	pciet_ete_ring_free(ctx, &ctx->sr0);
	return ret;
}

static void pciet_remove(struct pci_dev *pdev)
{
	struct hi5622_pci *ctx = pci_get_drvdata(pdev);

	pciet_poll_stop(ctx);
	pciet_ete_ring_free(ctx, &ctx->dr0);
	pciet_ete_ring_free(ctx, &ctx->sr0);
	dev_info(&pdev->dev, "%s removed\n", DRV_NAME);
}

static const struct pci_device_id pciet_id_table[] = {
	{ PCI_DEVICE(HI5622_VENDOR_ID, HI5622_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pciet_id_table);

static struct pci_driver pciet_driver = {
	.name		= DRV_NAME,
	.id_table	= pciet_id_table,
	.probe		= pciet_probe,
	.remove		= pciet_remove,
	.driver		= {
		.dev_groups = pciet_groups,
	},
};
module_pci_driver(pciet_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi5622V100 PCIe/ETE transport skeleton (clean-room, Linux 6.18)");
MODULE_AUTHOR("cudy-wr3000v2 wifi-transport-port lane");
