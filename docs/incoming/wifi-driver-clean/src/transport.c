// SPDX-License-Identifier: GPL-2.0
/*
 * transport.c - PCI transport core of the Hi5622V100 clean-room Wi-Fi driver.
 *
 * ORIGINAL clean-room code (Linux 6.18), built from the project's decoded
 * contract (opensource/docs/incoming/wifi-transport-port, wifi-rom-dump,
 * wifi-s1-cpu1, wifi-fw-postboot, and the 2026-11-24 GP3..GP8 bench runs).
 * No vendor source text, object code, firmware byte or calibration value is
 * reproduced; the Wi-Fi PHY custom block is loaded from a local firmware
 * directory and is never committed.
 *
 * WHAT THIS FILE DOES
 *   1. Program the six endpoint-iATU inbound viewports through BAR2 (the
 *      vendor order, with read-back verification).
 *   2. Upload the firmware image through the kernel firmware loader into the
 *      region-5 window, then verify it word-by-word at the target window.
 *   3. Pre-seed the two configuration blocks the image's entry A consumes
 *      (platform descriptor + Wi-Fi custom block) BEFORE the release.
 *   4. Release the CPU with 0x5A5A ONLY.  The boot-stage handshake word
 *      CA 0x4000010C must keep the mask-ROM 0xCEED value: writing 0xCECE
 *      during boot clobbers it and CPU1's S1 stage never completes (root
 *      cause found 2026-11-24 10:00; GP8 golden boot 10:40).
 *   5. Expose read-only boot discriminators and the one guarded repair write
 *      (0xCEED, pre-S1 only) that the ROM reconstruction allows.
 *
 * Every device write is a single explicit sysfs action (probe performs no
 * device writes); CA 0x400392F0 is never written (build-time + runtime gate).
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hi5622_driver.h"

/* =====================================================================
 * Six inbound regions (programming order = vendor order)
 * ===================================================================*/
const struct hi5622_inbound_region hi5622_regions[HI5622_NUM_REGIONS] = {
	/* idx 0: ROM_WRAM - 1.75 MiB (phase18 inbound-map A.6); the inherited
	 * transport header had 0x001bfff here (a one-digit typo that truncated
	 * the window by 0x1b4000); corrected to the vendor size 0x1c0000. */
	{ 0x0000000, 0x001bffff, 0x00000000 },
	/* idx 1: TCM_NOACP */
	{ 0x01c0000, 0x01d7fff, 0x00400000 },
	/* idx 2: PKTRAM_NOACP */
	{ 0x01d8000, 0x03b7fff, 0x01000000 },
	/* idx 3: IO - registers, ETE engine, mailbox */
	{ 0x03b8000, 0x04d7fff, 0x40000000 },
	/* idx 4: ACP */
	{ 0x04d8000, 0x06b7fff, 0x02000000 },
	/* idx 5: firmware RAM (image target CA 0x01240000 lives here) */
	{ 0x06b8000, 0x08cffff, 0x01200000 },
};

/* dev_status_check read-outs (firmware-re sec 2.5, [V*]) */
const struct hi5622_dev_status_entry hi5622_dev_status[HI5622_NUM_DEV_STATUS] = {
	{ "efuse_chip_id",    0x3b82a8 },
	{ "dcoldo_efuse",     0x3b82d4 },
	{ "dcoldo_vset",      0x3bd00c },
	{ "pbank_code",       0x3bd05c },
	{ "abank_code",       0x3bd060 },
	{ "temp",             0x3fc110 },
	{ "lock_status",      0x3fc208 },
	{ "pcie0_status",     0x3f1224 },
	{ "pcie0_latch",      0x3f1220 },
	{ "tcxo_pll_mux_sel", 0x4b9230 },
	{ "tcxo_pll_status",  0x4b9234 },
};

/* =====================================================================
 * Platform custom-block descriptor (24 words / 0x60 B)
 *
 * INI-derived descriptor (device cfg: firmware_plat_custom_src_addr 0x1b2800,
 * len 0x60; TCM section bounds) verified word-for-word against the read-only
 * stock capture of 2026-11-24 05:00 (ledger).  It is boot configuration, not
 * PHY calibration; the Wi-Fi PHY block after it (0x9a8 B) is a separate,
 * locally-staged file and is never committed.
 * ===================================================================*/
static const u32 hi5622_platform_block[HI5622_BLOCK_PLAT_WORDS] = {
	0x00000000,	/* +0x00 reserved */
	0x00117bc4,	/* +0x04 firmware_itcm_src_addr [V] */
	0x0000a968,	/* +0x08 firmware_itcm_len     [V] */
	0x0012252c,	/* +0x0c firmware_dtcm_src_addr [V] */
	0x0000076c,	/* +0x10 firmware_dtcm_len     [V] */
	0x001b2800,	/* +0x14 platform block self address */
	0x00000060,	/* +0x18 platform block size */
	0x001b2860,	/* +0x1c wifi-custom block address */
	0x000009a8,	/* +0x20 wifi-custom block size */
	0x00000002,	/* +0x24 boot mode word (stock capture value 2) */
	0x00000001,	/* +0x28 boot config */
	0x00000028,	/* +0x2c boot config */
	0x00000064,	/* +0x30 boot config */
	0x000001cc,	/* +0x34 boot config */
	0xffffc92d,	/* +0x38 boot config */
	0xffffffc0,	/* +0x3c boot config */
	0x000001da,	/* +0x40 boot config */
	0xffffc97d,	/* +0x44 boot config */
	0xffffffa0,	/* +0x48 boot config */
	0x000001ed,	/* +0x4c boot config */
	0xffffc92d,	/* +0x50 boot config */
	0xffffffc0,	/* +0x54 boot config */
	0x80000000,	/* +0x58 RC inbound window base */
	0x00000000,	/* +0x5c reserved */
};

/* =====================================================================
 * Compile-time contract asserts
 * ===================================================================*/
static void hi5622_transport_assert_contract(void)
{
	BUILD_BUG_ON(sizeof(struct hi5622_ete_node) != 8);
	BUILD_BUG_ON(ARRAY_SIZE(hi5622_regions) != 6);
	BUILD_BUG_ON(ARRAY_SIZE(hi5622_dev_status) != 11);
	BUILD_BUG_ON(HI5622_IO_BAR_BASE + (HI5622_IO_CA_HI - HI5622_IO_CA_LO) > 0x4d8000);
	BUILD_BUG_ON(HI5622_FW_TARGET_BAR0 + HI5622_FW_CHUNK > 0x6b8000 + 0x218000);
	BUILD_BUG_ON((int)HI5622_BLOCK_PLAT_WORDS * 4 != HI5622_BLOCK_PLAT_BYTES);
	BUILD_BUG_ON((int)HI5622_BLOCK_WIFI_WORDS * 4 != HI5622_BLOCK_WIFI_BYTES);
	/* Region-3 translation self-check: the mailbox / D2H / out[N] / ETE
	 * CAs must map with the true 21-bit region offset (finding F4), and no
	 * writable register may alias the never-write word out[5]. */
	BUILD_BUG_ON(HI5622_CA_NEVER_WRITE < HI5622_IO_CA_LO ||
		     HI5622_CA_NEVER_WRITE > HI5622_IO_CA_HI);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_H2D_PENDING) != 0x3f1010u);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_D2H_BITMAP) != 0x3f1014u);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_H2D_DOORBELL) != 0x3f12d4u);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_D2H_ACK) != 0x4b9438u);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_D2H_REARM) != 0x4b9414u);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_GATE) != 0x3b810cu);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_ETE_INTR) != 0x3f1508u);
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_NEVER_WRITE) ==
		     HI5622_BAR0_CA(HI5622_CA_H2D_PENDING));
	BUILD_BUG_ON(HI5622_BAR0_CA(HI5622_CA_NEVER_WRITE) ==
		     HI5622_BAR0_CA(HI5622_CA_D2H_ACK));
}

/* The single device-write helper below is intentionally not exported: every
 * writer in this file is a gated, logged, explicitly-attended action. */
static void hi5622_write32(struct hi5622_pci *ctx, u32 bar0_off, u32 val)
{
	if (WARN_ON_ONCE(bar0_off == hi5622_io_bar0(HI5622_CA_NEVER_WRITE)))
		return;
	writel(val, ctx->bar0 + bar0_off);
}

/* Range-checked read helpers (see hi5622_driver.h for the mapping rules). */
u32 hi5622_reg_read_ca(struct hi5622_pci *ctx, u32 ca)
{
	u32 off = hi5622_ca_to_bar0(ca);

	if (!off && ca != hi5622_regions[0].ca_target) {
		dev_warn_once(&ctx->pdev->dev,
			      "register read: device CA 0x%08x is not host-visible through the six inbound regions (refused; the event controller at 0x40160000 is NOT mapped)\n",
			      ca);
		return 0;
	}
	return readl(ctx->bar0 + off);
}

u32 hi5622_fw_ram_read(struct hi5622_pci *ctx, u32 cpu)
{
	u32 off = hi5622_fw_ram_bar0(cpu);

	if (!off && cpu)
		return 0;
	return readl(ctx->bar0 + off);
}

bool hi5622_boot_complete(struct hi5622_pci *ctx)
{
	if (!ctx->windows_programmed)
		return false;
	return hi5622_transport_page(ctx, 0x8) == HI5622_HANDSHAKE_ACK;
}

bool hi5622_golden_boot(struct hi5622_pci *ctx)
{
	if (!hi5622_boot_complete(ctx))
		return false;
	if (hi5622_transport_page(ctx, 0xc) != HI5622_HANDSHAKE_MAGIC)
		return false;
	if (hi5622_fw_ram_read(ctx, HI5622_CPU_BOOT_STATE) != 1)
		return false;
	if (hi5622_fw_ram_read(ctx, HI5622_CPU_EXCP_MASK) != 0)
		return false;
	return true;
}

u32 hi5622_transport_page(struct hi5622_pci *ctx, u32 off)
{
	if (!ctx->windows_programmed)
		return 0;
	return readl(ctx->bar0 + HI5622_HANDSHAKE_BAR0 + off);
}

/* =====================================================================
 * 1. Endpoint iATU window programming (phase18 A.4-A.6, [V]/[V*])
 *
 * iATU base/limit are ABSOLUTE PCI bus addresses: the endpoint translates
 * the incoming BAR0 address range, so the window values include the BAR0
 * resource start.  Writes happen in the vendor order, then every word is
 * read back and logged.
 * ===================================================================*/
int hi5622_transport_prepare_windows(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	void __iomem *iatu = ctx->bar2;
	u64 bar0 = pci_resource_start(ctx->pdev, HI5622_BAR_MEM_WIN);
	u32 i, blk, val;
	int all_ok = 1;

	dev_info(dev, "iATU: programming %d inbound viewports through BAR2 (endpoint), BAR0 start 0x%pap\n",
		 HI5622_NUM_REGIONS, &ctx->pdev->resource[HI5622_BAR_MEM_WIN].start);

	for (i = 0; i < HI5622_NUM_REGIONS; i++) {
		const struct hi5622_inbound_region *r = &hi5622_regions[i];
		u64 lo = bar0 + r->bar_base;
		u64 lim = bar0 + r->bar_limit;

		blk = HI5622_IATU_VP0_OFF + HI5622_IATU_VP_STRIDE * i;
		writel(0, iatu + blk + IATU_VP_CTRL2);
		writel(IATU_VP_CTRL2_ENA | IATU_VP_CTRL2_BAR(HI5622_BAR_MEM_WIN),
		       iatu + blk + IATU_VP_CTRL2);
		writel((u32)lo, iatu + blk + IATU_VP_BASE_LO);
		writel((u32)(lo >> 32), iatu + blk + IATU_VP_BASE_HI);
		writel((u32)lim, iatu + blk + IATU_VP_LIMIT_LO);
		writel(r->ca_target, iatu + blk + IATU_VP_TARGET_LO);
		writel(0, iatu + blk + IATU_VP_TARGET_HI);
	}

	for (i = 0; i < HI5622_NUM_REGIONS; i++) {
		const struct hi5622_inbound_region *r = &hi5622_regions[i];
		bool ok = true;

		blk = HI5622_IATU_VP0_OFF + HI5622_IATU_VP_STRIDE * i;
		val = readl(iatu + blk + IATU_VP_CTRL2);
		ok &= (val == (IATU_VP_CTRL2_ENA |
			       IATU_VP_CTRL2_BAR(HI5622_BAR_MEM_WIN)));
		ok &= (readl(iatu + blk + IATU_VP_BASE_LO) ==
		       (u32)(bar0 + r->bar_base));
		ok &= (readl(iatu + blk + IATU_VP_LIMIT_LO) ==
		       (u32)(bar0 + r->bar_limit));
		ok &= (readl(iatu + blk + IATU_VP_TARGET_LO) == r->ca_target);
		dev_info(dev, "iATU vp%u: ctrl2=0x%08x base=0x%06x limit=0x%06x target=0x%08x %s\n",
			 i, val, r->bar_base, r->bar_limit, r->ca_target,
			 ok ? "readback=OK" : "readback=MISMATCH");
		all_ok &= ok;
	}
	ctx->windows_programmed = all_ok;
	return all_ok ? 0 : -EIO;
}

/* =====================================================================
 * 2. Firmware upload + verify (firmware-re sec 2.1, [V]/[V*])
 *
 * fw_path is a kernel firmware-loader name RELATIVE to /lib/firmware (the
 * vendor name is hi_wifi/FIRMWARE.bin).  The image is written contiguously to
 * CA 0x01240000 through the region-5 window in 0x80000-byte chunks; no rings
 * and no per-chunk header are involved.
 * ===================================================================*/
int hi5622_transport_fw_upload(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	const struct firmware *fw;
	void __iomem *dst;
	size_t off = 0;
	int ret;

	if (ctx->cpu_released) {
		dev_err(dev, "upload: CPU already released; reload the module for a fresh boot\n");
		return -EBUSY;
	}
	ret = firmware_request_nowarn(&fw, hi5622_fw_path, dev);
	if (ret) {
		dev_err(dev, "upload: firmware loader cannot get %s under /lib/firmware (%d)\n",
			hi5622_fw_path, ret);
		return ret;
	}
	dev_info(dev, "upload %s: %zu bytes -> BAR0+0x%x (CA 0x%08x), chunk 0x%x\n",
		 hi5622_fw_path, fw->size, HI5622_FW_TARGET_BAR0,
		 HI5622_FW_TARGET_CA, HI5622_FW_CHUNK);

	dst = ctx->bar0 + HI5622_FW_TARGET_BAR0;
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
	ctx->fw_size = fw->size;
	release_firmware(fw);
	ctx->fw_loaded = true;
	ctx->fw_verified = false;
	dev_info(dev, "upload: %zu bytes in %zu chunks (verify is a separate gated step)\n",
		 off, DIV_ROUND_UP(off, HI5622_FW_CHUNK));
	return 0;
}

int hi5622_transport_fw_verify(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	const struct firmware *fw;
	size_t off, diffs = 0;
	int ret;

	if (!ctx->fw_loaded) {
		dev_err(dev, "verify: upload first\n");
		return -EBUSY;
	}
	ret = firmware_request_nowarn(&fw, hi5622_fw_path, dev);
	if (ret)
		return ret;
	for (off = 0; off + 4 <= fw->size; off += 4) {
		u32 exp = le32_to_cpup((const __le32 *)(fw->data + off));
		u32 got = readl(ctx->bar0 + HI5622_FW_TARGET_BAR0 + off);

		if (exp != got)
			diffs++;
	}
	dev_info(dev, "verify target BAR0+0x%x: file=%zu bytes diffs=%zu match=%s\n",
		 HI5622_FW_TARGET_BAR0, fw->size, diffs,
		 diffs ? "NO" : "YES");
	release_firmware(fw);
	if (diffs)
		return -EIO;
	ctx->fw_verified = true;
	return 0;
}

/* =====================================================================
 * 3. Custom-block pre-seed (GP3..GP8 contract; kprobe-verified vendor order)
 *
 * The vendor host writes the platform descriptor and the Wi-Fi PHY block
 * BEFORE the release word.  The platform descriptor is built in (INI-derived,
 * verified against the stock capture); the Wi-Fi block is vendor PHY/radio
 * configuration and is loaded from a LOCAL firmware file, never committed.
 * Both writes are read back word-for-word.
 * ===================================================================*/
static int hi5622_seed_write(struct hi5622_pci *ctx, u32 bar0, const u8 *data,
			     u32 bytes, const char *what)
{
	struct device *dev = &ctx->pdev->dev;
	u32 i, diffs = 0;

	for (i = 0; i + 4 <= bytes; i += 4) {
		u32 w = le32_to_cpup((const __le32 *)(data + i));

		hi5622_write32(ctx, bar0 + i, w);
	}
	for (i = 0; i + 4 <= bytes; i += 4) {
		u32 w = le32_to_cpup((const __le32 *)(data + i));

		if (readl(ctx->bar0 + bar0 + i) != w)
			diffs++;
	}
	dev_info(dev, "seed %s: %u B to BAR0+0x%x, readback diffs=%u\n",
		 what, bytes, bar0, diffs);
	return diffs ? -EIO : 0;
}

static int hi5622_seed_platform(struct hi5622_pci *ctx)
{
	int ret;

	if (hi5622_plat_block_path[0]) {
		const struct firmware *fw;

		ret = firmware_request_nowarn(&fw, hi5622_plat_block_path,
					      &ctx->pdev->dev);
		if (ret)
			return ret;
		if (fw->size != HI5622_BLOCK_PLAT_BYTES) {
			dev_err(&ctx->pdev->dev, "seed platform: %s is %zu B, expected %u\n",
				hi5622_plat_block_path, fw->size,
				HI5622_BLOCK_PLAT_BYTES);
			release_firmware(fw);
			return -EINVAL;
		}
		ret = hi5622_seed_write(ctx, HI5622_BLOCK_PLAT_BAR0, fw->data,
					HI5622_BLOCK_PLAT_BYTES, "platform block");
		release_firmware(fw);
	} else {
		ret = hi5622_seed_write(ctx, HI5622_BLOCK_PLAT_BAR0,
					(const u8 *)hi5622_platform_block,
					HI5622_BLOCK_PLAT_BYTES,
					"platform block (built-in INI descriptor)");
	}
	if (!ret)
		ctx->platform_seeded = true;
	return ret;
}

static int hi5622_seed_wifi(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	const struct firmware *fw;
	int ret;

	if (!hi5622_wifi_block_path[0]) {
		dev_warn(dev, "seed wifi: no path configured (hi5622_wifi_block_path empty)\n");
		return -EINVAL;
	}
	ret = firmware_request_nowarn(&fw, hi5622_wifi_block_path, dev);
	if (ret) {
		dev_err(dev, "seed wifi: cannot get %s under /lib/firmware (%d); stage the captured PHY block locally (see tools/mkblocks.py)\n",
			hi5622_wifi_block_path, ret);
		return ret;
	}
	if (fw->size != HI5622_BLOCK_WIFI_BYTES) {
		dev_err(dev, "seed wifi: %s is %zu B, expected %u\n",
			hi5622_wifi_block_path, fw->size,
			HI5622_BLOCK_WIFI_BYTES);
		release_firmware(fw);
		return -EINVAL;
	}
	ret = hi5622_seed_write(ctx, HI5622_BLOCK_WIFI_BAR0, fw->data,
				HI5622_BLOCK_WIFI_BYTES, "wifi-custom block");
	release_firmware(fw);
	if (!ret)
		ctx->wifi_seeded = true;
	return ret;
}

int hi5622_transport_seed_blocks(struct hi5622_pci *ctx)
{
	int ret;

	if (ctx->cpu_released) {
		dev_err(&ctx->pdev->dev, "seed: CPU already released; reload the module for a fresh boot\n");
		return -EBUSY;
	}
	ret = hi5622_seed_platform(ctx);
	if (ret)
		return ret;
	ret = hi5622_seed_wifi(ctx);
	if (ret)
		return ret;
	ctx->blocks_seeded = true;
	dev_info(&ctx->pdev->dev, "seed: platform + wifi-custom blocks written and verified before release\n");
	return 0;
}

/* =====================================================================
 * 4. CPU release - 0x5A5A ONLY (GP8 corrected protocol)
 *
 * The 16-bit CA 0x4000010C is a dual-use register: the mask ROM's CPU0
 * fallback writes 0xCEED after seeing the release and CPU1's S1 stage waits
 * for it; the image's exception handler clears it and spins for 0xCECE only
 * after boot.  Therefore a 0xCECE write during boot is forbidden.
 * ===================================================================*/
int hi5622_transport_release(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;

	if (ctx->cpu_released) {
		dev_err(dev, "release: already released this module load; reload for a fresh boot\n");
		return -EBUSY;
	}
	if (!ctx->windows_programmed) {
		dev_err(dev, "release: run 'prepare' (iATU windows) first\n");
		return -EIO;
	}
	if (!ctx->fw_loaded) {
		dev_err(dev, "release: upload the firmware first\n");
		return -EIO;
	}
	if (!ctx->fw_verified) {
		dev_err(dev, "release: verify the upload first (diffs must be 0)\n");
		return -EIO;
	}
	if (!ctx->platform_seeded) {
		dev_err(dev, "release: platform custom block not seeded; run 'seed' first\n");
		return -EIO;
	}
	if (!ctx->wifi_seeded && !hi5622_allow_unseeded_release) {
		dev_err(dev, "release: wifi-custom block not seeded; run 'seed' first (or set hi5622_allow_unseeded_release=1 for a documented diagnostic boot)\n");
		return -EIO;
	}
	if (hi5622_boot_complete(ctx)) {
		dev_err(dev, "release: handshake page +8 already 2 - refusal (a second release is never needed)\n");
		return -EBUSY;
	}

	dev_info(dev, "release: 0x%04x -> CA 0x%08x (BAR0+0x%x); RELEASE-ONLY protocol: no 0xCECE write before page+8==2\n",
		 HI5622_CPU_RELEASE_MAGIC, HI5622_CA_CPU_RELEASE,
		 hi5622_io_bar0(HI5622_CA_CPU_RELEASE));
	hi5622_write32(ctx, hi5622_io_bar0(HI5622_CA_CPU_RELEASE),
		       HI5622_CPU_RELEASE_MAGIC);
	ctx->cpu_released = true;
	/* Arm the protocol core's LATCHED boot watch (F1): the core never
	 * writes the release word itself. */
	hi5622_hcc_note_release(ctx);
	return 0;
}

/* Guarded boot-stage repair: only allowed for the known-good ROM stage
 * (CPU released, handshake not complete yet, ROM magic present) and only
 * when the 16-bit gate is not already 0xCECE.  This is the optional
 * "if +8 stays 0" repair from the ROM reconstruction - never a CECE write. */
int hi5622_transport_gate_repair(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	u32 bar0 = hi5622_io_bar0(HI5622_CA_GATE);
	u16 gate;

	if (!ctx->windows_programmed)
		return -EIO;
	if (!ctx->cpu_released) {
		dev_warn(dev, "gate repair: refused before the release (ROM boot stage not started)\n");
		return -EBUSY;
	}
	if (hi5622_boot_complete(ctx)) {
		dev_warn(dev, "gate repair: refused - S1 already completed (page+8==2); the ROM stage is over\n");
		return -EBUSY;
	}
	gate = readw(ctx->bar0 + bar0);
	if (gate == (u16)HI5622_GATE_CECE) {
		dev_warn(dev, "gate repair: refused - gate reads 0xCECE (post-boot image value), not a ROM stage\n");
		return -EBUSY;
	}
	dev_info(dev, "gate repair: 16-bit 0x%04x -> 0x%04x at CA 0x%08x (ROM-stage 0xCEED repair; CPU1 S1 consumer only)\n",
		 gate, (u16)HI5622_GATE_CEED, HI5622_CA_GATE);
	writew((u16)HI5622_GATE_CEED, ctx->bar0 + bar0);
	ctx->gate_repair_done = true;
	return 0;
}

/* =====================================================================
 * 5. dev_status_check and read-only sysfs surface
 * ===================================================================*/
int hi5622_transport_dev_status_show(struct hi5622_pci *ctx, char *buf,
				     size_t len)
{
	int i, pos = 0;

	if (!ctx->windows_programmed) {
		pos += scnprintf(buf + pos, len - pos,
				 "transport: windows=0 (run 'prepare' first)\n");
		return pos;
	}
	for (i = 0; i < HI5622_NUM_DEV_STATUS; i++)
		pos += scnprintf(buf + pos, len - pos, "%s=0x%08x\n",
				 hi5622_dev_status[i].name,
				 readl(ctx->bar0 + hi5622_dev_status[i].bar0_off));
	return pos;
}

static ssize_t prepare_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = hi5622_transport_prepare_windows(ctx);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(prepare);

static ssize_t upload_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = hi5622_transport_fw_upload(ctx);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(upload);

static ssize_t verify_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = hi5622_transport_fw_verify(ctx);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(verify);

static ssize_t seed_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = hi5622_transport_seed_blocks(ctx);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(seed);

static ssize_t release_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = hi5622_transport_release(ctx);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(release);

static ssize_t gate_repair_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int ret = hi5622_transport_gate_repair(ctx);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(gate_repair);

static ssize_t status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int pos = 0;

	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "transport: windows=%d fw_loaded=%d fw_verified=%d fw_size=%zu platform_seeded=%d wifi_seeded=%d blocks_seeded=%d cpu_released=%d gate_repair=%d\n",
			 ctx->windows_programmed, ctx->fw_loaded,
			 ctx->fw_verified, ctx->fw_size, ctx->platform_seeded,
			 ctx->wifi_seeded, ctx->blocks_seeded,
			 ctx->cpu_released, ctx->gate_repair_done);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "params: fw_path=%s wifi_block_path=%s allow_unseeded_release=%u\n",
			 hi5622_fw_path, hi5622_wifi_block_path,
			 hi5622_allow_unseeded_release);
	return pos;
}
static DEVICE_ATTR_RO(status);

/* Boot discriminators: handshake page, S1 state, post-boot firmware objects,
 * the dual-use gate word and the mailbox words.  READ-ONLY by construction. */
static ssize_t boot_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	int pos = 0;
	u32 page_bar0 = HI5622_HANDSHAKE_BAR0;
	u32 gate_bar0 = hi5622_io_bar0(HI5622_CA_GATE);

	if (!ctx->windows_programmed) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "boot: windows not programmed (run 'prepare' first)\n");
		return pos;
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "handshake page CA 0x%08x = 0x%08x / 0x%08x / 0x%08x / 0x%08x (+8==2:%d golden:%d)\n",
			 HI5622_HANDSHAKE_CPU,
			 readl(ctx->bar0 + page_bar0 + 0x0),
			 readl(ctx->bar0 + page_bar0 + 0x4),
			 readl(ctx->bar0 + page_bar0 + 0x8),
			 readl(ctx->bar0 + page_bar0 + 0xc),
			 hi5622_boot_complete(ctx), hi5622_golden_boot(ctx));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "gate CA 0x%08x (16-bit read) = 0x%04x (0xceed = ROM boot stage, 0xcece = image handler post-boot)\n",
			 HI5622_CA_GATE, readw(ctx->bar0 + gate_bar0));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "state CA 0x%08x=0x%08x mask CA 0x%08x=0x%08x boot_slot CA 0x%08x=0x%08x\n",
			 HI5622_CPU_BOOT_STATE,
			 hi5622_fw_ram_read(ctx, HI5622_CPU_BOOT_STATE),
			 HI5622_CPU_EXCP_MASK,
			 hi5622_fw_ram_read(ctx, HI5622_CPU_EXCP_MASK),
			 HI5622_CPU_BOOT_SLOT,
			 hi5622_fw_ram_read(ctx, HI5622_CPU_BOOT_SLOT));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "g+0xb4 CA 0x%08x=0x%08x g+0xb8 CA 0x%08x=0x%08x (0x40 = deferred d2h notify word)\n",
			 HI5622_CPU_G_ARMED,
			 hi5622_fw_ram_read(ctx, HI5622_CPU_G_ARMED),
			 HI5622_CPU_G_PENDING,
			 hi5622_fw_ram_read(ctx, HI5622_CPU_G_PENDING));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "mailbox: out0=0x%08x out1=0x%08x out2=0x%08x out3(read)=0x%08x out4(read)=0x%08x out5(READ-ONLY)=0x%08x d2h_doorbell=0x%08x\n",
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_PENDING)),
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_D2H_BITMAP)),
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_H2D_DOORBELL)),
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_D2H_ACK)),
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_D2H_REARM)),
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_NEVER_WRITE)),
			 readl(ctx->bar0 + hi5622_io_bar0(HI5622_CA_D2H_DOORBELL)));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "note: the GIC/event-controller block CA 0x40160000 is NOT host-visible through the six inbound regions; this driver never fabricates a read for it (firmware-side observation only)\n");
	return pos;
}
static DEVICE_ATTR_RO(boot);

static ssize_t dev_status_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));

	return hi5622_transport_dev_status_show(ctx, buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(dev_status);

static struct attribute *hi5622_transport_attrs[] = {
	&dev_attr_prepare.attr,
	&dev_attr_upload.attr,
	&dev_attr_verify.attr,
	&dev_attr_seed.attr,
	&dev_attr_release.attr,
	&dev_attr_gate_repair.attr,
	&dev_attr_status.attr,
	&dev_attr_boot.attr,
	&dev_attr_dev_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hi5622_transport);

int hi5622_transport_add_attrs(struct device *dev)
{
	hi5622_transport_assert_contract();
	return device_add_groups(dev, hi5622_transport_groups);
}

void hi5622_transport_del_attrs(struct device *dev)
{
	device_remove_groups(dev, hi5622_transport_groups);
}
