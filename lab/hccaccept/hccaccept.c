// SPDX-License-Identifier: GPL-2.0
/*
 * hccaccept (phase 22, task st_01a0fbea): bothep plus the exp-harness batch
 * convention, to attack the device-side H2D HCC accept gate (out[0] CA
 * 0x40039010 never cleared by the chip, no id-1 reply).  See the phase-22
 * additions block near the end of this file and docs/phase22/
 * {NEXT-EXPERIMENT,exp-harness,h2d-accept}.md.
 *
 * Proven configuration carried verbatim from bothep boot 2 (docs/phase21/
 * both-eps.md):
 *   0000:00:00.0 primary   (domain=0, irq 207) - rings/release through its BAR
 *   0001:00:00.0 secondary (domain2=1, irq2=209) - the live completion INTx
 *   both RCs' six inbound + one outbound iATU viewports
 *   firmware load + 0x5a5a release + post-release SR re-assert + SR pump
 *
 * bothep: claim BOTH PCIe functions and decode BOTH RCs (phase 21b).
 *
 * The last two lanes split the two halves of the vendor's message path:
 *   - endpoint 0 `0000:00:00.0` (domain 0, phy_devid 1, irq 207): the SR
 *     engine FETCHES the descriptors (phase 20f txpath: SR+0x1c 0x10 -> 0x400)
 *     but its INTx never fires (0 on a live vendor boot);
 *   - endpoint 1 `0001:00:00.0` (domain 1, phy_devid 0, irq 209): the INTx
 *     fires (irq_taken in the thousands) but the SR engine never fetches
 *     (SR+0x1c frozen at 0x10).
 * The two functions alias one on-chip register space but sit behind two root
 * complexes; the descriptor fetch is a device DMA read that traverses ep0's
 * RC.  This module therefore keeps the registry programming on the sibling's
 * alias (domain 1, irq 209) and additionally claims `domain2` (default 0),
 * maps its BAR0/BAR2, and programs its six inbound + one outbound iATU
 * viewport, so the device's fetch path (ep0's RC) is decoded while the
 * interrupt stays on 209.
 *
 * Part A live evidence (normal vendor boot, read-only): BOTH endpoints' BAR2
 * iATU are fully programmed with the same outbound window (devva
 * 0x80000000..0xffffffff -> host 0x80000000) and the same six inbound
 * viewports, shifted by the 0x18000000 host offset.  The vendor's single
 * driver decodes both RCs; our earlier single-endpoint takeovers decoded only
 * one, which is why neither ever had both halves at once.
 *
 * New module parameters over srt: domain2/hostirq2/useirq2/irq2 (the second
 * endpoint) and seq (the in-boot BAR sequence test: program the SR ring
 * through the primary BAR, poll, then through the secondary BAR, polling
 * SR+0x1c and logging which BAR's write coincided with the fetch).
 *
 * Everything else (the two quoted binding writes, the firmware load/release,
 * the ETE SR/DR rings, the DR posts, the SR pump, the post-release SR
 * re-assert, enable=0) is carried from lab/srt verbatim.
 *
 * sr2 header follows.
 *
 * sr2: the phase-21 sibling-endpoint bring-up (0001:00:00.0, domain 1,
 * irq 209) with the corrected post-release ring/instance programming and the
 * in-thread SR producer pump.  See docs/phase21/sr-sibling.md Part A.
 *
 * The one correction over lab/sibep: the device's firmware rewrites the ETE
 * channel register block when it boots on release (proven live: SR+0x00 0 -> 1
 * and SR+0x18 0x400 -> 0x4 across the release on the sibling boot), so the
 * pre-release SR program is desynchronised from the device's indices and the
 * pump finds no free nodes.  After release this module re-asserts the quoted
 * program (SR base +0x10, depth +0x14, ctrl +0x08) and re-syncs the host
 * producer (+0x18) to the device's current consumer (+0x1c) per channel, then
 * pumps.  Every register it writes is quoted from pcie_ete_sr_reg_init
 * @0x14a48 / pcie_ete_chn_res @0x7490 / pcie_ete_intr_init @0x7528.
 *
 * Why: live-binding proved that the vendor's message interrupt
 * (`pcie_intr_handle` @0x82e4 reading the glue status CA 0x400392ec) is
 * delivered only on irq 209 = `0001:00:00.0`; `0000:00:00.0` (irq 207) takes
 * zero interrupts even on a fully working vendor boot.  The two PCIe
 * functions alias the same on-chip register space, and the ETE/glue instance
 * the vendor initialises and services (0x40039000 / 0x40039508 / 0x4003a000)
 * is the one our takeover has been driving - but it is reachable through
 * either function and the interrupt is asserted only on the sibling.  So this
 * module is `lab/bind` with the endpoint selection changed to domain 1 and
 * the IRQ changed to 209 (both are the module defaults now); every other step
 * is carried verbatim.
 *
 * bind header follows.
 *
 * srpump: svc (phase 21 service thread) + the in-thread SR producer pump.
 *
 * The phase-21 svc run stood the vendor service thread up and proved the D2H
 * message path live, but the chip never cleared out[0] (the H2D mask): the
 * thread only re-rang the id-3 doorbell, it never performed the vendor's SR
 * descriptor FILL + producer-index COMMIT from the thread.  The vendor's
 * pcie_thread_handle @0x16b00 calls pcie_ete_sending_trigger @0x13f90 per tx
 * queue after the device's id-6 wake; that walks the packed producer/consumer
 * indices and fills nodes until depth descriptors are outstanding, then commits
 * the producer.  This module adds exactly that pump (omo_pump_sr): on every
 * thread iteration it computes outstanding = (producer - consumer) mod
 * (2*depth) as the vendor does, re-fills the free SR nodes with the vendor's
 * 72-byte id-1 frame (slot 1 keeps the alg get_2g_power_param frame), advances
 * the packed producer index with pcie_ete_ring_ptr_plus @0x13ef8, commits
 * SR+0x18 (shuangta_ete_sr_dscr_fill @0x17858 / pcie_ete_sr_reg_init @0x14a48)
 * and rings pcie_msg_send(chip,3) @0x160f4.  It also reads the non-zero
 * [[ctx+4]]+0x2ec candidate (CA 0x400002ec) alongside the ETE block's +0x2ec.
 *
 * It keeps svc's takeover (claim/decode/load/release, the six inbound + one
 * outbound iATU viewports, the firmware, the message context, the SR/DR rings,
 * the DR post, the host message service) and enable=0 (never touch out[5]).
 *
 * fwaccept: the device-side gate that makes the chip's HCC message service
 * accept a host->device frame (phase 21, from phase 20f txpath).
 *
 * txpath (phase 20f) proved the host->device SR path is correct and live: the
 * endpoint's SR engine consumed all 32 posted descriptors and the H2D mask
 * out[0] (CA 0x40039010) read back the sent bit - yet the firmware never
 * cleared out[0] and never answered, because the asynchronous pcie_msg_send
 * path omits the H2D interrupt arm that the vendor's synchronous
 * pcie_msg_send_irq @0x174a8 performs (out[5] CA 0x400392f0 <= 8).  This
 * module is txpath plus that one quoted gate write (`enable=1`), or the
 * per-channel ETE control write (`enable=2`), then the same SR post, doorbell,
 * and poll - see docs/phase21/fw-accept.md.
 *
 * txpath header follows.
 *
 * txpath: the vendor's host->device SR transmit path + receive loop (phase 20f).
 *
 * This is rxloop (phase 20e, kept verbatim below) plus the host->device
 * transmit path named as the next blocker by docs/phase20/rx-loop.md:
 *
 *   - the SR node fill and its producer-index commit.  shuangta_ete_sr_dscr_fill
 *     @0x17858 writes an 8-byte node {word0 = message buffer device address,
 *     word1 = (len<<16)|0x6000|0xd2b} and rings pcie_msg_send(chip,3);
 *     pcie_ete_sr_reg_init @0x14a48 commits the index to SR+0x18.  This module
 *     posts all 32 SR nodes on the three SR channels (blocks 0x400/0x450/0x4a0)
 *     with the intercepted first message in slot 0.
 *   - the doorbell: pcie_msg_send @0x160f4 writes the pending bitmap to out[0]
 *     (CA 0x40039010) and ORs bit 0 of out[2] (CA 0x400392d4).
 *   - the message itself: the vendor's first host->device SR frame, captured
 *     live from a fresh vendor boot (72 bytes, HCC id 1) - see
 *     docs/phase20/tx-path.md A.5.
 *
 * rxloop header follows.
 *
 * rxloop: the vendor's PCIe receive loop (phase 20e).
 *
 * Phase 20d (msghalf) proved the host half (ack/clear/re-arm + the real id-6
 * handler) runs but leaves the firmware's payload unmoved.  This module adds
 * the missing producer/consumer state recovered from plat.ko:
 *
 *   - the DR receive ring posted with the vendor's exact node format AND its
 *     producer index committed to the channel write-pointer register.  Every
 *     8-byte node is filled with the payload buffer's device address in word0
 *     and word1 = 0, exactly as shuangta_ete_dr_dscr_fill @0x1765c does; the
 *     commit writes the vendor's packed index (index[9:0] | phase[10], see
 *     pcie_ete_ring_ptr_plus @0x13ef8) to DR+0x38 exactly as
 *     pcie_ete_dr_reg_init @0x1483c does.  Phase 20d left DR+0x38 = 0, i.e.
 *     zero buffers posted from the device's point of view.
 *   - the vendor's completion consumer: pcie_ete_transfer_done_handle @0x8730
 *     (message id 3) -> pcie_ete_d2h_isr_handle @0x15c1c (mask 0x1f) ->
 *     pcie_rx_handle @0x164d0 -> pcie_ete_dr_get_uploadbuf @0x1515c ->
 *     pcie_ete_rcv_buff_check @0x14d74.  A completion is the device advancing
 *     the DR read index (DR+0x3c); the payload lands in node.word0's buffer
 *     and carries the HCC header magic 0x5a5a at +0xa that rcv_buff_check
 *     tests.  The live idle vendor boot has DR+0x38 == DR+0x3c and all 32
 *     nodes {word0 = buffer CA, word1 = 0} - the node itself is never stamped
 *     (measured, 000_live_dr_crosscheck.txt).
 *
 * Direct successor to lab/hostwin, which programmed the device->host window
 * and the ETE rings and observed the firmware's out[1] = 0x40 (bit 6) word but
 * still no payload and irq_taken = 0.  This module adds the missing host half
 * (docs/phase20/msg-host-half.md):
 *
 *   - the ack / clear / re-arm contract of pcie_msg_handle @0x171f8, with the
 *     CAs recovered from the instruction stream: ctx = comm+0x2c (proven by
 *     the handler table at ctx+0x20 == comm+0x4c), so pending = out[1]
 *     (CA 0x40039014), ack = out[3] (CA 0x40101438) and re-arm = out[4]
 *     (CA 0x40101414).  The previous reports bound ack/re-arm to out[0]/out[2]
 *     as an inference; that is corrected here.
 *   - the real id-6 handler pcie_trigger_ete_sending_handle @0x15efc, which
 *     tail-calls pcie_wkup_thread @0x1629c: set comm+0x28 = 1 and wake the
 *     HCC receive thread (comm+0x18).  It writes no device register; we set
 *     the flag and read the DR ring.
 *   - DR receive buffers posted with shuangta_ete_dr_dscr_fill @0x1765c
 *     (node.word0 = payload buffer device address) so a payload has a target.
 *
 * Everything hostwin did (claim/decode/firmware release, the six inbound
 * viewports, the outbound window, the SR/DR rings, the INTx request) is kept.
 * The single unproven-but-quoted write remains PCI_INTERRUPT_LINE = 0xcf.
 *
 * Historic header follows.
 *
 * Direct successor to lab/rtmsg.  rtmsg proved that with the six inbound iATU
 * viewports and the ETE SR/DR rings programmed the released firmware emits a
 * new PCIe word (out[1] = 0x40, bit 6 = pcie_trigger_ete_sending_handle) but no
 * payload ever lands in a ring we own, because the ring base is a HOST address
 * the device cannot reach: the endpoint's *outbound* iATU viewport (BAR2
 * offset 0x000) - the one that maps a device-visible address onto the host
 * window - was never programmed.  This module adds exactly that step, following
 * docs/phase20/host-window.md Part A:
 *
 *   hostca -> devva (pcie_hostca_to_devva @0xaefc):
 *       devva = win->devva_base + hostca - win->hostca_base
 *     where win = chip->[4]->[0xc4], a per-chiptype static descriptor
 *     (.data+0x1fb8 for chiptype 0) holding
 *       +0x00 devva_base  = 0x80000000   (+0x08 devva_end  = 0xffffffff)
 *       +0x10 hostca_base = 0x80000000   (so devva == hostca here)
 *
 *   outbound viewport (oal_pcie_set_outbound_by_membar, inlined in
 *     oal_pcie_set_inbound @0x9a38..0x9af4): at BAR2+0x000, in order
 *       ctrl1=0, ctrl2=0x80000000|bar, base=win->devva_base,
 *       base_hi, limit=win->devva_end, target=win->hostca_base, target_hi
 *     Live vendor boot reads: base 0x80000000, limit 0xffffffff, target
 *     0x80000000.
 *
 * This module keeps the rtmsg message context, the ETE SR/DR rings and the
 * IRQ request, but: (1) programs the outbound viewport so the endpoint can
 * reach the host window, (2) converts every ring/descriptor host address with
 * the recovered hostca->devva formula before writing it to the device, and
 * (3) writes the vendor's observed PCI_INTERRUPT_LINE (0xcf = 207) into config
 * space when a takeover boot left it at 0xff - the one unproven-but-quoted
 * write - then requests that line.
 *
 * The phase-20a module (lab/msgd) proved that a boot-time takeover can claim the
 * endpoint, program the six inbound iATU viewports, load FIRMWARE.bin, release
 * the chip (0x5a5a -> CA 0x40000108) and observe exactly one mailbox word
 * (out[1], CA 0x40039014, 0 -> 4).  It could not service that word: the vendor
 * pipeline needs the *runtime message context* and the *ETE SR/DR rings* that
 * pcie_msg_init @0xb6e4 and pcie_ete_init @0x7820 build, plus an interrupt line.
 *
 * This module stands those up following docs/phase20/runtime-msg.md Part A:
 *
 *   message context (pcie_msg_init):  six mailbox CAs -> reg[0..5], an
 *     11-entry {fn,arg} handler table (kmalloc 0x58), the per-chip dispatch
 *     binding chip[i]+0x60..0x6c (pcie_msg_send_irq / ctx / pcie_msg_handle /
 *     ctx+0x2c), and the pcie_msg_handle contract: pending = *(ctx+4),
 *     ack = *(ctx+0xc) = 1, re-arm = *(ctx+0x10) = 1, dispatch lowest set bit
 *     of the pending mask through table[bit] (ctx+0x20).
 *
 *   ETE rings (pcie_ete_init / pcie_ete_init_src_ring / _dst_ring):
 *     3 SR channels {0x400,0x450,0x4a0} stride 0x114, 4 DR channels
 *     {0x590,0x5e0,0x630,0x680} stride 0x6c, depth 32, node = 8 bytes
 *     {u32 buffer address; u32 (len<<16)|flags}.  SR node array (depth+2)*8,
 *     DR node array depth*8, both coherent DMA.  Program order is base,
 *     depth-1, wptr, ctrl (SR) / base, depth-1, wptr (DR), then the +0x2e8
 *     read-modify-write (& 0xfffffc20) in pcie_ete_chn_res @0x7490.
 *
 *   IRQ: read PCI_INTERRUPT_LINE and request_irq(irq, IRQF_SHARED) exactly as
 *     do_request_irq @0x1081c.  The handler is deliberately defensive: it
 *     returns IRQ_NONE unless a watched register changed, and disables the
 *     (shared, level) line after the first hit so an un-cleared source cannot
 *     storm.  The ISR's job in the vendor is to clear the PCIe glue status
 *     (+0x2ec & 0x3d8, oal_pcie_transfer_done @0x83e4 / pcie_intr_handle
 *     @0x82e4) and then run pcie_msg_handle; we cannot reach that glue from the
 *     takeover, so we do not pretend to.
 *
 * Write policy: every device write is quoted from the disassembly or a live
 * vendor read.  The iATU viewports (six inbound + the one outbound), PCI_COMMAND,
 * the firmware, the 0x5a5a release and every SR/DR program register are quoted.
 * The ETE ring/descriptor addresses are now converted with the recovered
 * hostca->devva formula rather than written raw; the window values themselves
 * are quoted from the vendor's live iATU and from .data+0x1fb8.  The single
 * unproven-but-quoted write is the PCI_INTERRUPT_LINE byte (0xcf = 207) that a
 * vendor boot has and a takeover boot leaves at 0xff - no cfg 0x3c store exists
 * in plat.ko, so *how* it is set is inferred, only the value is measured.
 * There is no host->device message reply: the vendor handshake has none.
 *
 * Module parameters:
 *   domain=N   endpoint domain (default 0)
 *   program=N  1 = program the six inbound iATU viewports (default)
 *   fwpath=P   firmware path
 *   target=N   BAR0 offset the firmware is written to (default 0x6f8000)
 *   chunk=N    bytes per firmware chunk (default 0x80000)
 *   maxlen=N   limit bytes written (0 = whole file)
 *   release=N  1 = perform the 0x5a5a release write (default)
 *   irq=N      IRQ to request; 0 = read PCI_INTERRUPT_LINE (default 0)
 *   useirq=N   1 = request_irq the endpoint line (default)
 *   rings=N    1 = allocate + program the ETE SR/DR rings (default)
 *   acpoff=N   added to the coherent DMA address for the ETE ring base
 *              (device-VA window compensation; 0 = write the DMA address as-is)
 *   pollms=N   ms between polls (default 500)
 *   polldur=N  total ms to poll (default 25000)
 *   scanbase=N BAR0 offset of the firmware-RAM change scan (default 0x7d8000)
 *   scanlen=N  bytes of the change scan (default 0x60000, 0 = off)
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_BAR_NUM	0
#define OMO_IATU_BAR	2
#define OMO_IATU_STRIDE	0x200UL
#define OMO_IATU_CTRL2	0x104UL

#define FW_TARGET_OFF	0x6f8000UL	/* device CA 0x01240000 */
#define RELEASE_OFF	0x3b8108UL	/* device CA 0x40000108 (region 3) */
#define RELEASE_VAL	0x00005a5aU

#define MAILBOX_N	6
#define MSG_HANDLER_N	11
#define STATUS_N	12
#define SCAN_CHANGES_MAX 24
#define ISR_LOG_MAX	8

/* ETE register block (docs/phase17/ete-init.md A.1): static .data+0x2944.
 *
 * Device CA 0x4003a000 is reached through the region-3 viewport (host
 * 0x403b8000 -> dev CA 0x40000000), so its BAR0 offset is
 * 0x3b8000 + (0x4003a000 - 0x40000000) = 0x3f2000.  The phase-17 text's
 * "BAR0+0x3a000" conflated the device CA with a flat BAR0 offset. */
#define ETE_BAR0_OFF	0x3f2000UL
#define ETE_WIN_LEN	0x1000UL
#define ETE_CHN_RES	0x2e8
#define ETE_CHN_RES_MASK 0xfffffc20U
/* The message/glue block: device CA 0x40039000 -> BAR0+0x3f1000 (region-3
 * viewport 0x403b8000->CA 0x40000000; out[0] CA 0x40039010 = BAR0+0x3f1010).
 * Captured live on a normal vendor boot: pcie_ete_chn_res @0x7490 writes
 * CA 0x400392e8 (= block+0x2e8) with old & 0xfffffc20, and pcie_intr_handle
 * @0x82e4 reads CA 0x400392ec (= block+0x2ec) as the glue status.  The
 * phase-17/21 code put both on the ETE ring block CA 0x4003a000, which is 0. */
#define GLUE_BAR0_OFF		0x3f1000UL
#define GLUE_CHN_RES		0x2e8
#define GLUE_CHN_RES_MASK	0xfffffc20U
#define GLUE_STAT		0x2ec
#define GLUE_STAT_MASK		0x3d8U
/* ETE interrupt block: pcie_ete_intr_init @0x7528 maps ETE res+4 =
 * CA 0x40039508 (= BAR0+0x3f1508) and writes old & 0xffe0f8f8.  Live vendor
 * value 0x3f201818.  Never programmed by the earlier takeover. */
#define ETE_INTR_OFF		0x3f1508UL
#define ETE_INTR_MASK		0xffe0f8f8U
/* pcie_ete_intr_init @0x7528 reads the reset value 0x3f3f1f1f and writes
 * old & 0xffe0f8f8 = 0x3f201818 on the live vendor boot.
 *
 * Boot-order finding (Part A): on the vendor boot the firmware is already up
 * when the host driver runs pcie_ete_init (dmesg: wlan_power_on 13.08 then
 * pcie_main_init 13.08), so the host's write to this ETE interrupt block is
 * the LAST one.  This takeover instead loads+releases the firmware AFTER the
 * pre-release write, so the firmware's own pcie_msg_init (file 0x96b0 clears
 * bits 12/29, file 0x96f6 ANDs 0xe0e0f8f8 from the literal at 0x97c8) has the
 * last word and clears the ETE interrupt enables.  Re-assert the quoted vendor
 * target after the firmware boots. */
#define ETE_INTR_RESET		0x3f3f1f1fU
#define ETE_INTR_VENDOR		(ETE_INTR_RESET & ETE_INTR_MASK)
/* oal_pcie_transfer_done @0x83e4 / pcie_intr_handle @0x82e4: the ETE glue
 * status word is read (dsb first), masked with 0x3d8, and each set bit is
 * dispatched; the DMA-completion path clears a set bit by writing it back
 * (write-1-to-clear).  Register and mask are quoted; the base (the ETE block)
 * is inferred from the only ETE register window this endpoint exposes. */
#define ETE_GLUE_STAT	0x2ec
#define ETE_GLUE_MASK	0x3d8U
/* The two parked candidates for [[ctx+4]]+0x2ec.  In the live SOC register dump
 * (dumps/reg_all.txt) the ETE block CA 0x4003a000+0x2ec reads 0, while the
 * SSU/PCIe glue block CA 0x400002ec reads 0x2292 (mask 0x3d8 -> 0x90) - the only
 * +0x2ec word that is ever non-zero.  CA 0x400002ec is reached through the
 * region-3 IO viewport: BAR0 offset 0x3b8000 + (0x400002ec - 0x40000000). */
#define ETE_GLUE_SOC_OFF 0x3b82ecUL	/* CA 0x400002ec, the non-zero candidate */
#define ETE_SR_CTRL	0x08
#define ETE_SR_BASE	0x10
#define ETE_SR_DEPTH	0x14
#define ETE_SR_WPTR	0x18
#define ETE_SR_RPTR	0x1c
#define ETE_DR_BASE	0x30
#define ETE_DR_DEPTH	0x34
#define ETE_DR_WPTR	0x38
#define ETE_DR_RPTR	0x3c
#define ETE_DEPTH	32
#define ETE_SR_N	3
#define ETE_DR_N	4
#define ETE_DR_PAYLOAD	2048
#define ETE_SR_PAYLOAD	512	/* per-node host->device SR message buffer */
#define ETE_SR_FLAG	0x6d2b	/* shuangta_ete_sr_dscr_fill @0x17858: (len<<16)|0x6000|0xd2b */
#define ETE_SR_MSG_LEN	0x48	/* vendor's first SR message, live capture (72 B) */
#define ETE_SR_ALG_LEN	0x12a	/* alg get_2g_power_param H2D frame, live capture (298 B) */
#define MSG_SEND_ID	3	/* pcie_msg_send(chip,3) after an SR fill (0x178f8) */
#define MSG_RECLAIM_ID	5	/* pcie_msg_send(chip,5), rcv_buff_check (0x15140) */

static const unsigned long omo_sr_block[ETE_SR_N] = { 0x400, 0x450, 0x4a0 };
static const unsigned long omo_dr_block[ETE_DR_N] = { 0x590, 0x5e0, 0x630, 0x680 };

struct omo_region {
	unsigned long off;
	u32 size;
	u64 target;
	const char *name;
};

static const struct omo_region omo_regions[6] = {
	{ 0x000000, 0x1c0000, 0x00000000UL, "ROM_WRAM" },
	{ 0x1c0000, 0x018000, 0x00400000UL, "TCM_NOACP" },
	{ 0x1d8000, 0x1e0000, 0x01000000UL, "PKTRAM_NOACP" },
	{ 0x3b8000, 0x120000, 0x40000000UL, "IO" },
	{ 0x4d8000, 0x1e0000, 0x02000000UL, "ACP" },
	{ 0x6b8000, 0x218000, 0x01200000UL, "ACP-fw" },
};

static unsigned int omo_domain = 1;   /* 1 = 0001:00:00.0, the irq-209 sibling */
module_param_named(domain, omo_domain, uint, 0444);
static unsigned int omo_program = 1;
module_param_named(program, omo_program, uint, 0444);
static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
static unsigned long omo_target = FW_TARGET_OFF;
module_param_named(target, omo_target, ulong, 0444);
static unsigned int omo_chunk = 0x80000;
module_param_named(chunk, omo_chunk, uint, 0444);
static unsigned int omo_maxlen;
module_param_named(maxlen, omo_maxlen, uint, 0444);
static unsigned int omo_release = 1;
module_param_named(release, omo_release, uint, 0444);
static int omo_irq;
module_param_named(irq, omo_irq, int, 0444);
static unsigned int omo_useirq = 1;
module_param_named(useirq, omo_useirq, uint, 0444);
static unsigned int omo_rings = 1;
module_param_named(rings, omo_rings, uint, 0444);
static int omo_acpoff;
module_param_named(acpoff, omo_acpoff, int, 0444);
static unsigned int omo_outwin = 1;
module_param_named(outwin, omo_outwin, uint, 0444);
static unsigned int omo_devva_base = 0x80000000U;
module_param_named(devvabase, omo_devva_base, uint, 0444);
static unsigned int omo_devva_end = 0xffffffffU;
module_param_named(devvaend, omo_devva_end, uint, 0444);
static unsigned int omo_hostca_base = 0x80000000U;
module_param_named(hostcabase, omo_hostca_base, uint, 0444);
static unsigned int omo_hostirq = 209;   /* sibling endpoint INTx (irq 209) */
module_param_named(hostirq, omo_hostirq, uint, 0444);
/* the second endpoint (phase 21b claims BOTH functions) */
static unsigned int omo_domain2 = 0;     /* 0000:00:00.0, the irq-207 function */
module_param_named(domain2, omo_domain2, uint, 0444);
static unsigned int omo_hostirq2 = 207;  /* endpoint-0 INTx (irq 207) */
module_param_named(hostirq2, omo_hostirq2, uint, 0444);
static unsigned int omo_useirq2 = 1;     /* request the second endpoint's line */
module_param_named(useirq2, omo_useirq2, uint, 0444);
static int omo_irq2;                     /* override; 0 = PCI_INTERRUPT_LINE */
module_param_named(irq2, omo_irq2, int, 0444);
static unsigned int omo_seq = 1;         /* in-boot BAR sequence test */
module_param_named(seq, omo_seq, uint, 0444);
static unsigned int omo_pollms = 500;
module_param_named(pollms, omo_pollms, uint, 0444);
static unsigned int omo_polldur = 25000;
module_param_named(polldur, omo_polldur, uint, 0444);
static unsigned long omo_scanbase = 0x7d8000UL;
module_param_named(scanbase, omo_scanbase, ulong, 0444);
static unsigned int omo_scanlen = 0x60000;
module_param_named(scanlen, omo_scanlen, uint, 0444);
/*
 * The device-side gate (Part A).  0 = log the channel/out[5] state but write
 * nothing (control boot); 1 = out[5] = 8 (pcie_msg_send_irq's arm word);
 * 2 = per-channel ETE block +0x00 bit0 and +0x48 = 1 (the vendor's live
 * channel-enable state); 3 = both.  At most one *new* unproven-but-quoted
 * write family is exercised per boot (0, 1 or 2).
 */
static unsigned int omo_enable = 1;
module_param_named(enable, omo_enable, uint, 0444);
/* Part A trigger: 0 = dump only, 1 = write the quoted vendor target
 * 0x3f201818 post-release (default), 2 = OR vendor bits, 3 = raw reset. */
static unsigned int omo_intr = 1;
module_param_named(intr, omo_intr, uint, 0444);
/* SR channel control +0x08 low 3 bits.  The vendor programs cfg[5] = 0
 * (pcie_ete_sr_reg_init @0x14ae8-c: bfi r2,r1,#0,#3 with cfg[5]=0; live vendor
 * reads 0); lab/sr2 wrote 1.  The live field-by-field diff is exactly this bit. */
static unsigned int omo_srctrl = 1;
module_param_named(srctrl, omo_srctrl, uint, 0444);
/* phase-21 service thread (pcie_process_thread @0x16efc emulation) */
static unsigned int omo_svc = 1;
module_param_named(svc, omo_svc, uint, 0444);
static unsigned int omo_svcdur = 25000;
module_param_named(svcdur, omo_svcdur, uint, 0444);
static unsigned int omo_svcms = 100;
module_param_named(svcms, omo_svcms, uint, 0444);
static unsigned int omo_svcdoorbell = 10;
module_param_named(svcdoorbell, omo_svcdoorbell, uint, 0444);

/* ---- phase-22 hccaccept: batch selector + hypothesis params ------------ */
/* batch=<list path>: parse <path> (label|hyp=..,arg=.. per line), run one
 * hypothesis per active line in THIS boot, write per-entry result.txt under
 * batchdir, then batch-summary.txt and batch.done (docs/phase22/
 * exp-harness.md, section 2.1).  Empty = single-hypothesis mode (BOOT B). */
static char *omo_batch;
module_param_named(batch, omo_batch, charp, 0444);
static char *omo_batchdir = "/tmp/omo-batch";
module_param_named(batchdir, omo_batchdir, charp, 0444);
/* single-mode hypothesis (see omo_hyp_apply): none/glue/intror/intrbit/
 * h3noop/h3full/h3zero/h3ctrl/h3ctrl48/out5.  Default: control. */
static char *omo_hyp = "";
module_param_named(hyp, omo_hyp, charp, 0444);
static unsigned int omo_arg;        /* bit index / ctrl value for the hyp */
module_param_named(arg, omo_arg, uint, 0444);
static unsigned int omo_hccwin = 2500;  /* per-entry sample window (ms) */
module_param_named(hccwin, omo_hccwin, uint, 0444);
/* single-mode persistent status file (survives a watchdog reset in /root) */
static char *omo_resultpath = "";
module_param_named(resultpath, omo_resultpath, charp, 0444);
static unsigned int omo_stopfirst = 1;  /* stop the batch at first change */
module_param_named(stopfirst, omo_stopfirst, uint, 0444);

static struct pci_dev *omo_dev;
static void __iomem *omo_bar0;
static void __iomem *omo_iatu;
static u64 omo_bar0_base;
static void __iomem *omo_bar0b;   /* second endpoint region-3 viewport */
static void __iomem *omo_iatub;   /* second endpoint iATU (BAR2) */
static u64 omo_bar0b_base;
static struct pci_dev *omo_dev2;
static u8 *omo_fw;
static size_t omo_fw_len;
static u8 *omo_scan_base;

/* The six HCC message/mailbox registers, from shuangta_pcie_msg_reg_map @0x1b1a0.
 * slot order is the vendor's out[0..5] = msg ctx +0x2c..+0x40. */
struct omo_mbox {
	unsigned long off;	/* BAR0 offset */
	u32 ca;
	const char *what;
};

static const struct omo_mbox omo_mbox[MAILBOX_N] = {
	{ 0x3f1010, 0x40039010, "out[0] H2D mask" },
	{ 0x3f1014, 0x40039014, "out[1] pending" },
	{ 0x3f12d4, 0x400392d4, "out[2] doorbell" },
	{ 0x4b9438, 0x40101438, "out[3] ack" },
	{ 0x4b9414, 0x40101414, "out[4] re-arm" },
	{ 0x3f12f0, 0x400392f0, "out[5] send irq" },
};

static const struct omo_stat {
	unsigned long off;
	u32 ca;
	const char *what;
} omo_stat[STATUS_N] = {
	{ 0x3b82a8, 0x400002a8, "efuse_chip_id" },
	{ 0x3bd00c, 0x4000500c, "dcoldo_vset" },
	{ 0x3bd05c, 0x4000505c, "pbank_code" },
	{ 0x3bd060, 0x40005060, "abank_code" },
	{ 0x3f1224, 0x40039224, "pcie0_status" },
	{ 0x3f1220, 0x40039220, "pcie0 latch" },
	{ 0x4b9230, 0x40101230, "tcxo_pll_mux_sel" },
	{ 0x4b9234, 0x40101234, "tcxo_pll_status" },
	{ 0x7dac18, 0x01322c18, "fw BSS +0x00" },
	{ 0x7dac1c, 0x01322c1c, "fw BSS +0x04" },
	{ 0x8c7ff0, 0x01417ff0, "region5 top word" },
	{ 0x6f8000, 0x01240000, "fw image word0" },
};

/* ---- the runtime message context (pcie_msg_init @0xb6e4) ---------------- */
struct omo_msg_handler {
	void (*fn)(void *);
	void *arg;
};

struct omo_msgctx {
	void __iomem *reg[MAILBOX_N];	/* ctx+0x2c..+0x40: the six CAs      */
	void __iomem *pending;		/* ctx+0x04: *(ctx+4) != 0 = pending */
	void __iomem *ack;		/* ctx+0x0c: write 1 to ack          */
	void __iomem *rearm;		/* ctx+0x10: write 1 to re-arm       */
	struct omo_msg_handler *table;	/* ctx+0x20: 11 x {fn,arg}           */
};

static struct omo_msgctx omo_ctx;
static unsigned int omo_ctx_ready;

/* ---- ETE ring state ----------------------------------------------------- */
static void *omo_sr_va[ETE_SR_N];
static dma_addr_t omo_sr_dma[ETE_SR_N];
static void *omo_sr_pay[ETE_SR_N];
static dma_addr_t omo_sr_pay_dma[ETE_SR_N];
static u8 *omo_sr_snap[ETE_SR_N];
static u8 *omo_sr_pay_snap[ETE_SR_N];
static u32 omo_sr_wr_idx[ETE_SR_N];     /* committed SR producer index (packed) */
static u32 omo_sr_rptr_last[ETE_SR_N];  /* last device SR index observed */
static unsigned int omo_sr_events;
static unsigned int omo_sr_posted;
static unsigned int omo_pumped;         /* nodes re-filled by the service thread */
static unsigned int omo_sr_msg_seen[ETE_SR_N];
static void *omo_dr_va[ETE_DR_N];
static dma_addr_t omo_dr_dma[ETE_DR_N];
static void *omo_dr_pay[ETE_DR_N];
static dma_addr_t omo_dr_pay_dma[ETE_DR_N];
static u8 *omo_dr_snap[ETE_DR_N];
static u8 *omo_pay_snap[ETE_DR_N];
static u32 omo_dr_wr_idx[ETE_DR_N];    /* committed producer index (packed) */
static u32 omo_dr_rptr_last[ETE_DR_N]; /* last device read index observed */
static unsigned int omo_pay_magic[ETE_DR_N];
static unsigned int omo_dr_events;      /* device-index advances + 0x5a5a hits */
static unsigned int omo_ete_ready;

/* ---- IRQ state ---------------------------------------------------------- */
static int omo_irq_num = -1;
static unsigned int omo_irq_ok;
static atomic_t omo_irq_count = ATOMIC_INIT(0);
static atomic_t omo_irq_hits = ATOMIC_INIT(0);
static unsigned int omo_isr_logs;
static unsigned int omo_irq_disabled;
static int omo_irq2_num = -1;
static unsigned int omo_irq2_ok;
static unsigned int omo_irq2_disabled;
static atomic_t omo_irq2_count = ATOMIC_INIT(0);
static atomic_t omo_irq2_hits = ATOMIC_INIT(0);
static unsigned int omo_isr_logs2;
static u32 omo_isr_seen[MAILBOX_N];

static u32 omo_mbox_last[MAILBOX_N];
static u32 omo_stat_last[STATUS_N];
static unsigned long omo_t0;
static unsigned int omo_msgs;

/* the host half (pcie_msg_handle / pcie_wkup_thread emulation) */
static unsigned int omo_send_flag;   /* pcie_wkup_thread: comm+0x28 = 1 */
static unsigned int omo_svc_count;   /* ack/clear/re-arm services performed */

/* phase-21 service thread (pcie_process_thread @0x16efc emulation) */
static struct task_struct *omo_svc_task;
static DECLARE_COMPLETION(omo_svc_done);
static unsigned int omo_svc_iters;
static unsigned int omo_glue_clears;
static u32 omo_glue_last;
static u32 omo_out0_last;
static u32 omo_out1_last;
static u32 omo_sr_idx_last[ETE_SR_N];
static u32 omo_sr_pcs_last[ETE_SR_N];
static u32 omo_sr_base_last[ETE_SR_N];
static u32 omo_dr_idx_last[ETE_DR_N];

static unsigned long omo_ms_now(void)
{
	return jiffies_to_msecs(jiffies - omo_t0);
}

static const char *omo_msgid_name(int bit)
{
	switch (bit) {
	case 1: return "pcie_dev_ready_msg_handle(stub)";
	case 3: return "pcie_ete_transfer_done_handle";
	case 5: return "pcie_ete_rcv_reclaim(send id5)";
	case 6: return "pcie_trigger_ete_sending_handle";
	case 7: return "pcie_trigger_ete_sending_handle";
	default: return "unregistered";
	}
}

/* ---- iATU programming (vendor membar path, as inbound.c/fwboot.c) ------ */

static void omo_iatu_wr(void __iomem *iatu, unsigned long off, u32 val,
		       const char *who, const char *name)
{
	u32 rb;

	iowrite32(val, iatu + off);
	rb = ioread32(iatu + off);
	pr_info("omo-hccaccept: %s iatu[0x%03lx] <= 0x%08x readback=0x%08x match=%s  (%s)\n",
		who, off, val, rb, rb == val ? "YES" : "NO", name);
}

static int omo_program_regions(void __iomem *iatu, u64 bar0_base, const char *who)
{
	unsigned int i;
	int programmed = 0;

	for (i = 0; i < ARRAY_SIZE(omo_regions); i++) {
		const struct omo_region *r = &omo_regions[i];
		u64 base = bar0_base + r->off;
		u64 limit = base + r->size - 1;
		unsigned long ctrl2 = OMO_IATU_CTRL2 + OMO_IATU_STRIDE * i;
		char nm[40];

		pr_info("omo-hccaccept: %s region %u %s: host 0x%llx..0x%llx -> dev 0x%llx size 0x%x\n",
			who, i, r->name, (unsigned long long)base,
			(unsigned long long)limit, (unsigned long long)r->target,
			r->size);

		scnprintf(nm, sizeof(nm), "r%u ctrl2=0", i);
		omo_iatu_wr(iatu, ctrl2, 0, who, nm);
		scnprintf(nm, sizeof(nm), "r%u ctrl2=ena", i);
		omo_iatu_wr(iatu, ctrl2, 0x80000000U, who, nm);
		scnprintf(nm, sizeof(nm), "r%u base_lo", i);
		omo_iatu_wr(iatu, ctrl2 + 4, (u32)base, who, nm);
		scnprintf(nm, sizeof(nm), "r%u base_hi", i);
		omo_iatu_wr(iatu, ctrl2 + 8, (u32)(base >> 32), who, nm);
		scnprintf(nm, sizeof(nm), "r%u limit", i);
		omo_iatu_wr(iatu, ctrl2 + 12, (u32)limit, who, nm);
		scnprintf(nm, sizeof(nm), "r%u target_lo", i);
		omo_iatu_wr(iatu, ctrl2 + 16, (u32)r->target, who, nm);
		scnprintf(nm, sizeof(nm), "r%u target_hi", i);
		omo_iatu_wr(iatu, ctrl2 + 20, (u32)(r->target >> 32), who, nm);
		programmed++;
	}
	return programmed;
}

/* ---- the device-visible host window (pcie_hostca_to_devva @0xaefc) ---- */

struct omo_svc {
	u32 devva_base;   /* win+0x00 */
	u32 devva_end;    /* win+0x08 */
	u32 hostca_base;  /* win+0x10 */
};

static struct omo_svc omo_win;

/* pcie_hostca_to_devva: devva = devva_base + hostca - hostca_base. */
static u32 omo_hostca_to_devva(u64 hostca)
{
	u64 devva;

	if (hostca < omo_win.hostca_base)
		return 0xffffffffU;
	devva = (u64)omo_win.devva_base + (hostca - omo_win.hostca_base);
	return (u32)devva;
}

/*
 * oal_pcie_set_outbound_by_membar (inlined in oal_pcie_set_inbound @0x9a38):
 * one outbound viewport at BAR2+0x000 that translates the device-visible range
 * devva_base..devva_end onto the host window hostca_base.  Without it the
 * endpoint drops every DMA to a host address - the missing piece from rtmsg.
 */
static void omo_program_outbound(void __iomem *iatu, const char *who)
{
	pr_info("omo-hccaccept: %s outbound viewport0: devva 0x%08x..0x%08x -> host 0x%08x (oal_pcie_set_outbound_by_membar @0x9a38)\n",
		who, omo_win.devva_base, omo_win.devva_end, omo_win.hostca_base);
	omo_iatu_wr(iatu, 0x000, 0, who, "ob ctrl1=0");
	omo_iatu_wr(iatu, 0x004, 0x80000000U, who, "ob ctrl2=ena|bar0");
	omo_iatu_wr(iatu, 0x008, omo_win.devva_base, who, "ob base_lo=devva_base");
	omo_iatu_wr(iatu, 0x00c, 0, who, "ob base_hi");
	omo_iatu_wr(iatu, 0x010, omo_win.devva_end, who, "ob limit=devva_end");
	omo_iatu_wr(iatu, 0x014, omo_win.hostca_base, who, "ob target_lo=hostca_base");
	omo_iatu_wr(iatu, 0x018, 0, who, "ob target_hi");
}

/* ---- firmware load + write (phase-18 verified path) ------------------- */

static int omo_load_fw(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t done = 0;

	f = filp_open(omo_fwpath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-hccaccept: filp_open(%s) failed %ld\n",
		       omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-hccaccept: bad firmware size %zu\n", omo_fw_len);
		filp_close(f, NULL);
		return -EINVAL;
	}
	omo_fw = vmalloc(omo_fw_len);
	if (!omo_fw) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	while (done < omo_fw_len) {
		n = kernel_read(f, omo_fw + done, omo_fw_len - done, &pos);
		if (n <= 0) {
			pr_err("omo-hccaccept: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-hccaccept: firmware file %s size=%zu bytes\n",
		omo_fwpath, omo_fw_len);
	return 0;
}

static void omo_verify_win(const char *name, unsigned long b0, size_t limit)
{
	size_t off, diffs = 0;
	long first = -1;
	u32 w;
	int k;

	for (off = 0; off + 4 <= limit; off += 4) {
		w = ioread32(omo_bar0 + b0 + off);
		for (k = 0; k < 4; k++) {
			u8 got = (w >> (8 * k)) & 0xff;
			u8 exp = omo_fw[off + k];

			if (got != exp) {
				if (first < 0)
					first = (long)(off + k);
				diffs++;
			}
		}
	}
	pr_info("omo-hccaccept: verify %s BAR0+0x%lx: file=%zu bytes diffs=%zu match=%s\n",
		name, b0, limit, diffs, diffs ? "NO" : "YES");
	if (first >= 0) {
		u32 w0 = ioread32(omo_bar0 + b0 + (first & ~3UL));

		pr_info("omo-hccaccept:   first diff @0x%lx file=0x%02x chip=0x%02x\n",
			first, omo_fw[first], (u8)(w0 >> (8 * (first & 3))));
	}
}

static int omo_write_fw(void)
{
	size_t limit = omo_maxlen ? min_t(size_t, omo_maxlen, omo_fw_len) : omo_fw_len;
	size_t off = 0;
	u32 probe = 0xdeadbeef, rb;

	iowrite32(probe, omo_bar0 + omo_target);
	rb = ioread32(omo_bar0 + omo_target);
	pr_info("omo-hccaccept: writability probe BAR0+0x%lx: wrote 0x%08x read 0x%08x match=%s\n",
		omo_target, probe, rb, rb == probe ? "YES" : "NO");
	if (rb != probe) {
		pr_err("omo-hccaccept: target window does not hold a write - refusing the bulk write\n");
		return -EIO;
	}
	pr_info("omo-hccaccept: download %zu bytes -> BAR0+0x%lx (device CA 0x01240000) in %u-byte chunks\n",
		limit, omo_target, omo_chunk);
	while (off < limit) {
		size_t n = min_t(size_t, omo_chunk, limit - off);

		memcpy_toio(omo_bar0 + omo_target + off, omo_fw + off, n);
		off += n;
		pr_info("omo-hccaccept: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off, limit, omo_target + off - n);
	}
	omo_verify_win("target", omo_target, limit);
	return 0;
}

/* ---- the runtime message context --------------------------------------- */

/* Forward decls: defined with the polling helpers below. */
static void omo_decode_mailbox(const char *what, u32 v);
static void omo_scan_dr(const char *tag);
static void omo_rx_handler(void *arg);
static void omo_send_doorbell(const char *tag, unsigned int id);

/*
 * The vendor's per-message handler table (pcie_msg_register @0x15fbc) holds
 * {fn,arg} records; pcie_msg_init @0xb6e4 registers ids 1/3/6/7.  The id-6
 * entry is pcie_trigger_ete_sending_handle @0x15efc, a bare 4-byte tail call
 * `b pcie_wkup_thread` with arg = comm (mov r3,r4 at 0xb848).  Its complete
 * action list (pcie_wkup_thread @0x1629c) is:
 *   0x0162a8: mov  r2, #1
 *   0x0162ac: str  r2, [r4, #0x28]   ; comm+0x28 = 1 ("ETE send pending" flag)
 *   0x0162b0: add  r0, r4, #0x18     ; waitqueue head comm+0x18
 *   0x0162b4: mov  r1, r2            ; TASK_NORMAL (1)
 *   0x0162bc: mov  r3, #0
 *   0x0162c0: b    __wake_up         ; wake pcie_process_thread @0x16efc
 * It writes NO device register: it flags and wakes the HCC receive thread,
 * which is what reads the DR ring (pcie_rx_handle @0x164d0).  We reproduce the
 * effect: set the flag and read the ring here and from the poll loop.
 */
static void omo_id6_handler(void *arg)
{
	(void)arg;
	pr_info("omo-hccaccept: [id6] pcie_trigger_ete_sending_handle @0x15efc -> pcie_wkup_thread @0x1629c\n");
	pr_info("omo-hccaccept: [id6]   comm+0x28 <= 1 (send flag); __wake_up(comm+0x18) HCC rx thread\n");
	omo_send_flag = 1;
	pr_info("omo-hccaccept: [id6]   receive path: reading the DR ring (pcie_rx_handle @0x164d0)\n");
	omo_scan_dr("id6");
}

static void omo_msg_handler_stub(void *arg)
{
	pr_info("omo-hccaccept: [msg-handler] id=%ld stub - no device action\n",
		(long)arg);
}

/*
 * Build the context the vendor's pcie_msg_init @0xb6e4 builds: map the six
 * CAs (shuangta_pcie_msg_reg_map @0x1b1a0), kmalloc the 11-entry table
 * (0x58 bytes) and register the four PCIe-level handlers (ids 1,3,6,7).
 *
 * The +4/+0xc/+0x10 register pointers ARE statically attributable:
 * pcie_msg_handle @0x171f8 reads its handler table from *(ctx+0x20), and
 * pcie_msg_init stores that table at comm+0x4c (0xb764: str r3,[r4,#0x4c]),
 * so ctx+0x20 == comm+0x4c forces ctx == comm+0x2c - the very array
 * shuangta_pcie_msg_reg_map fills with out[0..5].  Hence (all PROVEN):
 *   pending = *(ctx+4)   = out[1] = CA 0x40039014 (the register the chip wrote)
 *   ack     = *(ctx+0xc) = out[3] = CA 0x40101438
 *   re-arm  = *(ctx+0x10)= out[4] = CA 0x40101414
 *   table   = *(ctx+0x20)= comm+0x4c
 */
static int omo_msgctx_build(void)
{
	unsigned int i;

	for (i = 0; i < MAILBOX_N; i++)
		omo_ctx.reg[i] = omo_bar0 + omo_mbox[i].off;

	omo_ctx.table = kzalloc(sizeof(struct omo_msg_handler) * MSG_HANDLER_N,
				GFP_KERNEL);
	if (!omo_ctx.table)
		return -ENOMEM;
	omo_ctx.table[1].fn = omo_msg_handler_stub;
	omo_ctx.table[1].arg = (void *)1;
	omo_ctx.table[3].fn = omo_rx_handler;   /* pcie_ete_transfer_done_handle @0x8730 */
	omo_ctx.table[3].arg = (void *)3;
	omo_ctx.table[6].fn = omo_id6_handler;
	omo_ctx.table[6].arg = (void *)6;
	omo_ctx.table[7].fn = omo_id6_handler;   /* registered in the vendor too */
	omo_ctx.table[7].arg = (void *)7;

	omo_ctx.pending = omo_ctx.reg[1];
	omo_ctx.ack = omo_ctx.reg[3];
	omo_ctx.rearm = omo_ctx.reg[4];

	pr_info("omo-hccaccept: msg ctx @ %px (pcie_msg_init @0xb6e4; ctx = comm+0x2c):\n",
		&omo_ctx);
	for (i = 0; i < MAILBOX_N; i++)
		pr_info("omo-hccaccept:   ctx+0x%02x = %px  CA=0x%08x  %s\n",
			0x2c + 4 * i, omo_ctx.reg[i], omo_mbox[i].ca,
			omo_mbox[i].what);
	pr_info("omo-hccaccept:   ctx+0x04 pending -> out[1] CA 0x%08x (PROVEN)\n",
		omo_mbox[1].ca);
	pr_info("omo-hccaccept:   ctx+0x0c ack     -> out[3] CA 0x%08x (PROVEN)\n",
		omo_mbox[3].ca);
	pr_info("omo-hccaccept:   ctx+0x10 re-arm  -> out[4] CA 0x%08x (PROVEN)\n",
		omo_mbox[4].ca);
	pr_info("omo-hccaccept:   ctx+0x20 handler table = %px (11 x {fn,arg}, kmalloc 0x58; == comm+0x4c)\n",
		omo_ctx.table);
	pr_info("omo-hccaccept:   id6 handler = %pS (pcie_trigger_ete_sending_handle)\n",
		(void *)omo_ctx.table[6].fn);
	pr_info("omo-hccaccept:   host half armed: ack/clear/re-arm written on the first pending word\n");
	omo_ctx_ready = 1;
	return 0;
}

/*
 * The host half: pcie_msg_handle @0x171f8, with ctx = comm+0x2c.
 *   0x017248: ldr r3, [r4, #0xc]      ; ack     = *(ctx+0xc) = out[3]
 *   0x017254: str r1(=1), [r3]        ; ACK: write 1
 *   0x017258: ldr r3, [r4, #4]        ; pending = *(ctx+4)  = out[1]
 *   0x01725c: ldr r5, [r3]            ; read the one-bit-per-message mask
 *   0x017260: str r2(=0), [r3]        ; CLEAR: write 0
 *   0x0172a0: ldr r3, [r4, #0x10]     ; re-arm  = *(ctx+0x10) = out[4]
 *   0x0172ac: str r2(=1), [r3]        ; RE-ARM: write 1
 *   0x0172b4: rbit r6,r5; clz r6,r6   ; lowest set bit = message id (<=0xa)
 *   0x0172d8: ldr r3, [r4, #0x20]     ; table = *(ctx+0x20) = comm+0x4c
 *   0x0172dc: ldr sl, [r3, r6, lsl #3] ; fn
 *   0x0172f0: blx sl                  ; fn(arg); loop for every set bit
 */
static void omo_msg_service(const char *tag)
{
	u32 pending, ackrb, rearmrb;
	unsigned int n = 0;

	if (!omo_ctx_ready)
		return;
	pending = ioread32(omo_ctx.pending);
	if (!pending)
		return;

	pr_info("omo-hccaccept: [svc %s +%lums] pending out[1] CA=0x%08x = 0x%08x\n",
		tag, omo_ms_now(), omo_mbox[1].ca, pending);
	omo_decode_mailbox("pending", pending);

	/* 1. ACK: *(ctx+0xc) = 1 -> out[3], CA 0x40101438 */
	iowrite32(1, omo_ctx.ack);
	ackrb = ioread32(omo_ctx.ack);
	pr_info("omo-hccaccept: [svc %s] ACK   out[3] CA=0x%08x <= 0x00000001 readback=0x%08x\n",
		tag, omo_mbox[3].ca, ackrb);

	/* 2. CLEAR: *(ctx+4) = 0 -> out[1], CA 0x40039014 */
	iowrite32(0, omo_ctx.pending);
	pr_info("omo-hccaccept: [svc %s] CLEAR out[1] CA=0x%08x 0x%08x -> 0x00000000 readback=0x%08x\n",
		tag, omo_mbox[1].ca, pending, ioread32(omo_ctx.pending));

	/* 3. RE-ARM: *(ctx+0x10) = 1 -> out[4], CA 0x40101414 */
	iowrite32(1, omo_ctx.rearm);
	rearmrb = ioread32(omo_ctx.rearm);
	pr_info("omo-hccaccept: [svc %s] REARM out[4] CA=0x%08x <= 0x00000001 readback=0x%08x\n",
		tag, omo_mbox[4].ca, rearmrb);

	omo_svc_count++;

	/* 4. dispatch every set bit through the table (vendor loops rbit/clz) */
	while (pending && n < 32) {
		int id = __ffs(pending);

		pending &= pending - 1;
		n++;
		if (id < MSG_HANDLER_N && omo_ctx.table[id].fn) {
			pr_info("omo-hccaccept: [svc %s] dispatch id=%d fn=%pS arg=%px\n",
				tag, id, (void *)omo_ctx.table[id].fn,
				omo_ctx.table[id].arg);
			omo_ctx.table[id].fn(omo_ctx.table[id].arg);
		} else {
			pr_info("omo-hccaccept: [svc %s] id=%d has no handler (unregistered)\n",
				tag, id);
		}
	}
}

/* ---- the ETE SR/DR rings (pcie_ete_init @0x7820) ----------------------- */

static int omo_rings_alloc(void)
{
	unsigned int i;

	if (pci_set_dma_mask(omo_dev, DMA_BIT_MASK(32)))
		pr_warn("omo-hccaccept: pci_set_dma_mask(32) failed\n");
	if (pci_set_consistent_dma_mask(omo_dev, DMA_BIT_MASK(32)))
		pr_warn("omo-hccaccept: pci_set_consistent_dma_mask(32) failed\n");

	for (i = 0; i < ETE_SR_N; i++) {
		size_t sz = (ETE_DEPTH + 2) * 8;

		omo_sr_va[i] = dma_alloc_coherent(&omo_dev->dev, sz,
						  &omo_sr_dma[i], GFP_KERNEL);
		if (!omo_sr_va[i]) {
			pr_err("omo-hccaccept: SR ch%u dma_alloc_coherent(%zu) FAILED\n",
			       i, sz);
			return -ENOMEM;
		}
		omo_sr_pay[i] = dma_alloc_coherent(&omo_dev->dev,
						   ETE_DEPTH * ETE_SR_PAYLOAD,
						   &omo_sr_pay_dma[i], GFP_KERNEL);
		if (!omo_sr_pay[i]) {
			pr_err("omo-hccaccept: SR ch%u payload alloc FAILED\n", i);
			return -ENOMEM;
		}
		omo_sr_snap[i] = kzalloc(sz, GFP_KERNEL);
		omo_sr_pay_snap[i] = kzalloc(ETE_DEPTH * ETE_SR_PAYLOAD, GFP_KERNEL);
		if (!omo_sr_snap[i] || !omo_sr_pay_snap[i])
			return -ENOMEM;
		pr_info("omo-hccaccept: SR ch%u nodes=%zuB virt=%px dma=0x%llx  msgbuf=%px dma=0x%llx\n",
			i, sz, omo_sr_va[i], (unsigned long long)omo_sr_dma[i],
			omo_sr_pay[i], (unsigned long long)omo_sr_pay_dma[i]);
	}

	for (i = 0; i < ETE_DR_N; i++) {
		size_t sz = ETE_DEPTH * 8;

		omo_dr_va[i] = dma_alloc_coherent(&omo_dev->dev, sz,
						  &omo_dr_dma[i], GFP_KERNEL);
		if (!omo_dr_va[i]) {
			pr_err("omo-hccaccept: DR ch%u dma_alloc_coherent(%zu) FAILED\n",
			       i + 3, sz);
			return -ENOMEM;
		}
		omo_dr_pay[i] = dma_alloc_coherent(&omo_dev->dev, ETE_DR_PAYLOAD,
						   &omo_dr_pay_dma[i], GFP_KERNEL);
		if (!omo_dr_pay[i]) {
			pr_err("omo-hccaccept: DR ch%u payload alloc FAILED\n", i + 3);
			return -ENOMEM;
		}
		omo_dr_snap[i] = kzalloc(sz, GFP_KERNEL);
		omo_pay_snap[i] = kzalloc(ETE_DR_PAYLOAD, GFP_KERNEL);
		if (!omo_dr_snap[i] || !omo_pay_snap[i])
			return -ENOMEM;

		/* The DR nodes are left zeroed: the device fills them (word0 =
		 * buffer device VA, word1 = (len<<16)|flags), and the device VA
		 * conversion (pcie_hostca_to_devva) is not available in the
		 * takeover - see docs/phase20/runtime-msg.md A.2.  We own the
		 * arrays and payload buffers and scan them for a deposit. */
		pr_info("omo-hccaccept: DR ch%u nodes=%zuB virt=%px dma=0x%llx  payload=%px dma=0x%llx\n",
			i + 3, sz, omo_dr_va[i],
			(unsigned long long)omo_dr_dma[i], omo_dr_pay[i],
			(unsigned long long)omo_dr_pay_dma[i]);
	}

	omo_ete_ready = 1;
	return 0;
}

/*
 * Post device->host receive buffers the way the chip layer does:
 * shuangta_ete_dr_dscr_fill @0x1765c writes the buffer's device address into
 * the current DR node (`str r1,[r2,r3,lsl #3]`, index = [inst+0x1c] & 0x3ff):
 *   0x01765c: ldr r3, [r0, #0x1c]     ; current DR index
 *   0x017660: ldr r2, [r0, #0x10]     ; node array
 *   0x017664: ubfx r3, r3, #0, #0xa
 *   0x017668: str r1, [r2, r3, lsl #3]; node[index].word0 = buffer device VA
 * Without a posted node the engine has no target for a payload.  This touches
 * only our own coherent buffer (word0 = hostca->devva of the payload buffer,
 * word1 left 0 for the device to fill with len<<16|flags).  Host memory only.
 * The producer index is then committed to DR+0x38 exactly as
 * pcie_ete_dr_reg_init @0x1483c does (str [regblock+0x38] = [inst+0x1c]).
 */

/* pcie_ete_ring_ptr_plus @0x13ef8: packed index = index[9:0] | phase[10];
 * increment the index, and on reaching depth wrap it to 0 and toggle phase.
 *
 * pcie_ete_ring_ptr_plus @0x13ef8, faithfully:
 *   0x013efc: add  r2, r3, #1      ; r2 = idx + 1 (phase carry NOT masked)
 *   0x013f00: bfi  r3, r2, #0,#0xa ; r3[9:0] = (idx+1)[9:0], phase kept
 *   0x013f04: ubfx r2, r3, #0,#0xa ; index only
 *   0x013f08: cmp  r2, depth
 *   0x013f0c: bfceq r3, #0,#0xa    ; wrap index to 0
 *   0x013f10: ubfxeq r2, r3,#0xa,#1 ; phase
 *   0x013f14: eoreq r2, r2, #1      ; toggle
 *   0x013f18: bfieq r3, r2,#0xa,#1
 * The previous implementation masked the phase bit on the first step, so a
 * full-lap refill committed the SAME index and the device saw no new nodes. */
static u32 omo_ring_ptr_plus(u32 idx, u32 depth)
{
	u32 r3 = (idx & ~0x3ffu) | ((idx + 1) & 0x3ffu);

	if ((r3 & 0x3ffu) == depth)
		r3 = (r3 & ~0x3ffu) ^ 0x400u;	/* index 0, toggle the phase bit */
	return r3;
}

static void omo_post_dr(void)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i, j;

	if (!omo_ete_ready)
		return;
	for (i = 0; i < ETE_DR_N; i++) {
		u32 devva = omo_hostca_to_devva(omo_dr_pay_dma[i]) +
				(u32)omo_acpoff;
		u64 *nodes = omo_dr_va[i];
		u32 idx = 0, rb;

		for (j = 0; j < ETE_DEPTH; j++) {
			nodes[idx & 0x3ff] = devva;   /* word0 = devva, word1 = 0 */
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
		}
		omo_dr_wr_idx[i] = idx;
		iowrite32(idx, win + omo_dr_block[i] + ETE_DR_WPTR);
		rb = ioread32(win + omo_dr_block[i] + ETE_DR_WPTR);
		pr_info("omo-hccaccept: DR ch%u posted %u nodes word0=0x%08x (payload dma 0x%llx -> devva; dr_dscr_fill @0x1765c)\n",
			i + 3, ETE_DEPTH, devva,
			(unsigned long long)omo_dr_pay_dma[i]);
		pr_info("omo-hccaccept: DR ch%u commit DR+0x%02x (wptr) <= 0x%08x readback=0x%08x (packed index, dr_reg_init @0x1483c)\n",
			i + 3, (unsigned)ETE_DR_WPTR, idx, rb);
		pr_info("omo-hccaccept: DR ch%u baseline after commit: wptr(+0x38)=0x%08x rptr(+0x3c)=0x%08x (device index, before release)\n",
			i + 3, rb, ioread32(win + omo_dr_block[i] + ETE_DR_RPTR));
	}
}

/*
 * The vendor's first host->device SR message, captured live (read-only) from a
 * fresh vendor boot: SR ch0 node[0] of the ETE window on a healthy rox_pci0
 * boot, read through /dev/mem at the node's own device address.  It is the
 * earliest SR frame the vendor host posts, verbatim (72 bytes).  Layout:
 *   +0x00 u32 protocol/group (00 01 00 04)
 *   +0x04 u16 total len 0x0030, u16 HCC id 0x0001
 *   +0x08 u16 0x0000, u16 0x5a5a  (the header magic rcv_buff_check tests)
 *   +0x0c 8-byte token, +0x14 u16 0x00d8/u16 0x0014, +0x18 payload start.
 */
static const u8 omo_sr_msg[ETE_SR_MSG_LEN] = {
	0x00, 0x01, 0x00, 0x04, 0x30, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x5a, 0x5a, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0xd8, 0x00, 0x14, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/*
 * Post host->device SR nodes and commit the producer index.
 *
 * shuangta_ete_sr_dscr_fill @0x17858 (data branch) fills one 8-byte node:
 *   0x01789c: movw r1, #0xd2b           ; host-fill magic
 *   0x0178a0: orr  r2, r2, #0x4000      ; owner bit 14
 *   0x0178ac: orr  r2, r2, #0x2000      ; owner bit 13
 *   0x0178b8: bfi  r2, r1, #0, #0xd     ; word1[12:0] = 0xd2b
 *   0x0178cc: str  addr, [r3, idx, lsl #3]  ; word0 = buffer device address
 *   0x0178e0: str  r2, [r3, #4]             ; word1 = (len<<16)|0x6d2b
 *   0x0178f8: bl   pcie_msg_send            ; doorbell id 3
 * and pcie_ete_sr_reg_init @0x14a48 commits the index:
 *   0x014ad8: ldr r2, [r4, #0xc] ; str r2, [r3, #0x18]   ; SR+0x18 = producer
 * This fills node[0] with the captured first message and the rest with
 * 512-byte receive slots (all our own coherent host memory).
 */
static void omo_post_sr(void)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i, j;

	if (!omo_ete_ready)
		return;

	memset(omo_sr_pay[0], 0, ETE_DEPTH * ETE_SR_PAYLOAD);
	memcpy(omo_sr_pay[0], omo_sr_msg, sizeof(omo_sr_msg));
	{
		u32 *w = omo_sr_pay[0];

		pr_info("omo-hccaccept: SR ch0 first H2D message built (%zu B): proto=0x%08x len/id=0x%08x magic=0x%08x tok=0x%08x/%08x hdr14=0x%08x hdr18=0x%08x (live vendor capture, node[0])\n",
			sizeof(omo_sr_msg), w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
	}
	/*
	 * Slot 1: the vendor's alg get_2g_power_param host->device frame, captured
	 * live at hcc_msg_tx+0x20 (id 3, len 0x12a): proto 0x01200101,
	 * len/id 0x0003012a, magic 0x5a5a0000, token (0), cmd/len 0x010e0101,
	 * payload 0x0d010dae, then 1.  This is the earliest H2D command whose
	 * firmware response is proven (docs/phase5/message-decode.md).
	 */
	{
		u32 *w = (u32 *)((u8 *)omo_sr_pay[0] + ETE_SR_PAYLOAD);

		memset(w, 0, ETE_SR_PAYLOAD);
		w[0] = 0x01200101;
		w[1] = 0x0003012a;
		w[2] = 0x5a5a0000;
		w[5] = 0x010e0101;
		w[6] = 0x0d010dae;
		w[8] = 0x00000001;
		pr_info("omo-hccaccept: SR ch0 slot1 alg frame built (%u B): w0=0x%08x len/id=0x%08x magic=0x%08x cmd/len=0x%08x payload=0x%08x (live vendor capture)\n",
			ETE_SR_ALG_LEN, w[0], w[1], w[2], w[5], w[6]);
	}

	for (i = 0; i < ETE_SR_N; i++) {
		u32 devva = omo_hostca_to_devva(omo_sr_pay_dma[i]) +
				(u32)omo_acpoff;
		u64 *n = omo_sr_va[i];
		u32 idx = 0, rb;

		for (j = 0; j < ETE_DEPTH; j++) {
			u32 a = devva + j * ETE_SR_PAYLOAD;
			u32 ln = (j == 1) ? ETE_SR_ALG_LEN : ETE_SR_MSG_LEN;
			u64 w1 = (u64)(u32)((ln << 16) | ETE_SR_FLAG);

			n[j] = (w1 << 32) | a;   /* word0 = buf devva, word1 = (len<<16)|0x6d2b */
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
		}
		omo_sr_wr_idx[i] = idx;
		iowrite32(idx, win + omo_sr_block[i] + ETE_SR_WPTR);
		rb = ioread32(win + omo_sr_block[i] + ETE_SR_WPTR);
		pr_info("omo-hccaccept: SR ch%u posted %u nodes word0=0x%08x word1=0x%08x; commit SR+0x%02x <= 0x%08x readback=0x%08x (sr_dscr_fill @0x17858 / sr_reg_init @0x14a48)\n",
			i, ETE_DEPTH, devva,
			(u32)((ETE_SR_MSG_LEN << 16) | ETE_SR_FLAG),
			(unsigned)ETE_SR_WPTR, idx, rb);
		if (i == 0)
			pr_info("omo-hccaccept: SR ch0 node[1] (slot1) carries the alg get_2g_power_param frame, len 0x%x, word1=0x%08x\n",
				ETE_SR_ALG_LEN,
				(u32)((ETE_SR_ALG_LEN << 16) | ETE_SR_FLAG));
	}
	omo_sr_posted = 1;
}

/*
 * The SR producer pump the vendor's service thread runs (pcie_ete_sending_trigger
 * @0x13f90 walks the ring with exactly this outstanding calculation):
 *
 *   0x014004: eor  r3, r3, r8        ; producer ^ consumer
 *   0x014008: tst  r3, #0x400        ; phase bit differs?
 *   0x014024: subne r3, r3, r1       ; (producer & 0x3ff) - (consumer & 0x3ff)
 *   0x01402c: addne r3, r3, r2       ;   + depth
 *   0x014030: subeq r3, r3, r2       ; else producer&0x3ff - consumer&0x3ff
 * so outstanding = (producer - consumer) mod (2*depth).  The ring is refilled
 * up to depth, the producer index is advanced with the packed
 * index[9:0]|phase[10] walk (pcie_ete_ring_ptr_plus @0x13ef8), committed to
 * SR+0x18 exactly as pcie_ete_sr_reg_init @0x14a48 / shuangta_ete_sr_dscr_fill
 * @0x17858 do, and the id-3 doorbell is rung.  Each node is re-filled with the
 * vendor's 72-byte id-1 frame (slot 1 keeps the alg get_2g_power_param frame),
 * so the device has a steady supply of SR descriptors to consume.  Host memory
 * plus the quoted SR+0x18 commit only.
 */
static u32 omo_sr_outstanding(u32 prod, u32 cons)
{
	u32 pl = prod & 0x3ff, cl = cons & 0x3ff;

	if ((prod ^ cons) & 0x400)
		return (pl + ETE_DEPTH) - cl;
	return pl - cl;
}

/*
 * Post-release ring/instance correction (this module's one change over
 * lab/sibep).  The device's firmware rewrites the ETE channel register block
 * when it boots on release: on the sibling boot the module's pre-release
 * program read back SR+0x18 = 0x400, but at service time the same register
 * read 0x4 (ch0) / 0x2 (ch1,ch2) with SR+0x1c = 0x10 and SR+0x00 = 1 - the
 * device had re-driven the block, so the host producer the pump compared
 * against the device consumer was stale and outstanding came out negative on
 * the u32 subtraction (free = 0, pumped = 0).
 *
 * Re-assert the quoted program - pcie_ete_sr_reg_init @0x14a48 writes
 * SR+0x10 = hostca_to_devva(node array), SR+0x14 = depth-1, SR+0x18 =
 * producer, SR+0x08[2:0] = cfg[5] - and re-sync the host producer to the
 * device's current consumer (+0x1c) so the packed outstanding arithmetic
 * starts from a consistent (producer == consumer) ring.  Every value written
 * is quoted; the register base is the sibling's own region-3 viewport.
 */
static void omo_ete_resync_sr_win(const char *tag, void __iomem *win,
				  const char *who)
{
	unsigned int i;

	if (!omo_ete_ready)
		return;
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 want = omo_hostca_to_devva(omo_sr_dma[i]) + (u32)omo_acpoff;
		u32 base = ioread32(win + b + ETE_SR_BASE);
		u32 dep = ioread32(win + b + ETE_SR_DEPTH);
		u32 w = ioread32(win + b + ETE_SR_WPTR);
		u32 r = ioread32(win + b + ETE_SR_RPTR);
		u32 c = ioread32(win + b + ETE_SR_CTRL);

		pr_info("omo-hccaccept: [%s +%lums] %s SR ch%u post-release: base(+0x10)=0x%08x want=0x%08x depth(+0x14)=0x%08x ctrl(+0x08)=0x%08x wptr(+0x18)=0x%08x rptr(+0x1c)=0x%08x%s\n",
			tag, omo_ms_now(), who, i, base, want, dep, c, w, r,
			base != want ? "  -- BASE REWRITTEN BY DEVICE" : "");
		iowrite32(want, win + b + ETE_SR_BASE);
		iowrite32(ETE_DEPTH - 1, win + b + ETE_SR_DEPTH);
		iowrite32(r, win + b + ETE_SR_WPTR);
		iowrite32((c & ~0x7u) | (omo_srctrl & 0x7u), win + b + ETE_SR_CTRL);
		omo_sr_wr_idx[i] = r;
		omo_sr_base_last[i] = want;
		omo_sr_idx_last[i] = r;
		omo_sr_pcs_last[i] = r;
		pr_info("omo-hccaccept: [%s +%lums] %s SR ch%u re-asserted base=0x%08x wptr:=rptr=0x%08x ctrl(+0x08)=0x%08x (readbacks base=0x%08x wptr=0x%08x ctrl=0x%08x)\n",
			tag, omo_ms_now(), who, i, want, r,
			ioread32(win + b + ETE_SR_CTRL),
			ioread32(win + b + ETE_SR_BASE),
			ioread32(win + b + ETE_SR_WPTR),
			ioread32(win + b + ETE_SR_CTRL));
	}
}

static void omo_ete_resync_sr(const char *tag)
{
	omo_ete_resync_sr_win(tag, omo_bar0 + ETE_BAR0_OFF, "primary(ep1)");
}

static void omo_pump_sr_win(const char *tag, void __iomem *win, const char *who)
{
	unsigned int i;
	unsigned int total = 0;

	if (!omo_ete_ready)
		return;
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 wptr = ioread32(win + b + ETE_SR_WPTR);
		u32 rptr = ioread32(win + b + ETE_SR_RPTR);
		u32 outstanding = omo_sr_outstanding(wptr, rptr);
		u32 free = (outstanding < ETE_DEPTH) ? ETE_DEPTH - outstanding : 0;
		u32 idx = wptr;
		u64 *n = omo_sr_va[i];
		unsigned int posted = 0;

		while (posted < free) {
			u32 slot = idx & 0x3ff;
			u32 devva = omo_hostca_to_devva(omo_sr_pay_dma[i]) +
				(u32)omo_acpoff + slot * ETE_SR_PAYLOAD;
			u32 ln = (slot == 1) ? ETE_SR_ALG_LEN : ETE_SR_MSG_LEN;
			u64 w1 = (u64)(u32)((ln << 16) | ETE_SR_FLAG);

			n[slot] = (w1 << 32) | devva;
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
			posted++;
		}
		if (!posted)
			continue;
		omo_sr_wr_idx[i] = idx;
		iowrite32(idx, win + b + ETE_SR_WPTR);
		{
			u32 rb = ioread32(win + b + ETE_SR_WPTR);

			pr_info("omo-hccaccept: [pump %s %s +%lums] SR ch%u refilled %u node(s) (wptr=0x%08x rptr=0x%08x outstanding=%u) commit SR+0x18 <= 0x%08x readback=0x%08x (sr_dscr_fill @0x17858)\n",
				tag, who, omo_ms_now(), i, posted, wptr, rptr,
				outstanding, idx, rb);
		}
		total += posted;
	}
	if (total) {
		omo_pumped += total;
		omo_send_doorbell(tag, MSG_SEND_ID);
	}
}

static void omo_pump_sr(const char *tag)
{
	omo_pump_sr_win(tag, omo_bar0 + ETE_BAR0_OFF, "primary(ep1)");
}

/* Log an SR channel's indices through both region-3 viewports. */
static void omo_log_sr_pair(const char *tag, const char *step)
{
	unsigned int i;

	for (i = 0; i < ETE_SR_N; i++) {
		u32 pa_w = ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[i] + ETE_SR_WPTR);
		u32 pa_r = ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[i] + ETE_SR_RPTR);
		u32 pb_w = ioread32(omo_bar0b + ETE_BAR0_OFF + omo_sr_block[i] + ETE_SR_WPTR);
		u32 pb_r = ioread32(omo_bar0b + ETE_BAR0_OFF + omo_sr_block[i] + ETE_SR_RPTR);

		pr_info("omo-hccaccept: [seq %s %s +%lums] SR ch%u wptr/rptr: primary(ep1)=0x%08x/0x%08x secondary(ep0)=0x%08x/0x%08x%s\n",
			tag, step, omo_ms_now(), i, pa_w, pa_r, pb_w, pb_r,
			(pa_r != 0x10) ? "  -- DEVICE FETCHED" : "");
	}
}

/*
 * The in-boot BAR sequence test (module param seq=1).  Both RCs' outbound
 * windows and inbound viewports are already programmed by the time this runs.
 * Program/commit the SR ring through the PRIMARY (ep1) BAR first and poll
 * SR+0x1c for 2 s; if the engine still has not fetched, re-program/commit the
 * same quoted values through the SECONDARY (ep0) BAR and poll again.  Logs the
 * SR indices through BOTH viewports at every step, so the report can name the
 * BAR whose write coincided with the fetch.
 */
static void omo_seq_program(const char *tag)
{
	unsigned int k;

	if (!omo_ete_ready || !omo_seq)
		return;

	pr_info("omo-hccaccept: [seq %s] STEP 1 - ring program + commit via PRIMARY BAR0 (0x%llx); both RCs decoded\n",
		tag, (unsigned long long)omo_bar0_base);
	omo_ete_resync_sr_win(tag, omo_bar0 + ETE_BAR0_OFF, "primary/ep1 BAR0");
	omo_pump_sr_win("seq1", omo_bar0 + ETE_BAR0_OFF, "primary/ep1 BAR0");
	for (k = 0; k < 20; k++) {
		msleep(100);
		if (ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[0] + ETE_SR_RPTR) != 0x10)
			break;
	}
	omo_log_sr_pair(tag, "step1");

	if (ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[0] + ETE_SR_RPTR) != 0x10) {
		pr_info("omo-hccaccept: [seq %s] STEP 2 skipped - the device fetched after the PRIMARY write\n",
			tag);
		return;
	}

	pr_info("omo-hccaccept: [seq %s] STEP 2 - ring program + commit via SECONDARY BAR0 (0x%llx)\n",
		tag, (unsigned long long)omo_bar0b_base);
	omo_ete_resync_sr_win(tag, omo_bar0b + ETE_BAR0_OFF, "secondary/ep0 BAR0");
	omo_pump_sr_win("seq2", omo_bar0b + ETE_BAR0_OFF, "secondary/ep0 BAR0");
	for (k = 0; k < 20; k++) {
		msleep(100);
		if (ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[0] + ETE_SR_RPTR) != 0x10)
			break;
	}
	omo_log_sr_pair(tag, "step2");
}

/*
 * pcie_msg_send @0x160f4, the host->device doorbell:
 *   0x016194: ldr r2, [r6, #0x2c]  ; out[0] CA 0x40039010
 *   0x01619c: str r3, [r2]         ; write the pending bitmap (1<<id)
 *   0x0161a4: ldr r2, [r6, #0x34]  ; out[2] CA 0x400392d4
 *   0x0161a8: ldr r3, [r2]
 *   0x0161ac: orr r3, r3, #1
 *   0x0161b0: str r3, [r2]         ; doorbell |= 1
 */
static void omo_send_doorbell(const char *tag, unsigned int id)
{
	u32 mask = 1U << id;
	u32 m0, m2;

	m0 = ioread32(omo_bar0 + omo_mbox[0].off);
	iowrite32(mask, omo_bar0 + omo_mbox[0].off);
	pr_info("omo-hccaccept: [send %s] pcie_msg_send(chip,%u): out[0] CA=0x%08x 0x%08x -> 0x%08x readback=0x%08x (pcie_msg_send @0x160f4)\n",
		tag, id, omo_mbox[0].ca, m0, mask,
		ioread32(omo_bar0 + omo_mbox[0].off));
	m2 = ioread32(omo_bar0 + omo_mbox[2].off);
	iowrite32(m2 | 1U, omo_bar0 + omo_mbox[2].off);
	pr_info("omo-hccaccept: [send %s] out[2] CA=0x%08x 0x%08x -> 0x%08x readback=0x%08x (doorbell |= 1)\n",
		tag, omo_mbox[2].ca, m2, m2 | 1U,
		ioread32(omo_bar0 + omo_mbox[2].off));
}

/*
 * The device-side gate that our txpath boot never touched.
 *
 * mode 1 - out[5] CA 0x400392f0 (BAR0+0x3f12f0): the vendor's SYNCHRONOUS
 *   sender pcie_msg_send_irq @0x174a8 writes the literal 8 there before it
 *   flushes out[0]/out[2]:
 *     0x0174f8: ldr r3, [r4, #0x40]   ; out[5] CA 0x400392f0
 *     0x0174fc: mov r2, #8
 *     0x017504: str r2, [r3]          ; *out[5] = 8
 *   Our takeover uses only the asynchronous pcie_msg_send @0x160f4, which
 *   never writes out[5].  The firmware's own message dispatcher (file 0x818a8)
 *   uses ctx+0xc as its ACK word; the firmware's message context (built at
 *   file ~0x975c) stores out[5] (0x400392f0) at ctx+0xc, so 0x400392f0 is
 *   the H2D interrupt arm/ack register, not a data word.
 *
 * mode 2 - per-channel ETE control.  A live vendor boot reads +0x00 = 0x1 and
 *   +0x48 = 0x1 for all seven channels (barmap 0x4003a400/0x448,
 *   0x4003a450/0x498, 0x4003a4a0/0x4e8, 0x4003a590/0x5d8, 0x4003a5e0/0x628,
 *   0x4003a630/0x678, 0x4003a680/0x6c8).  Neither pcie_ete_sr_reg_init
 *   @0x14a48 nor pcie_ete_dr_reg_init @0x1483c nor pcie_ete_chn_res @0x7490
 *   writes either field, and the firmware's own ETE bring-up sets bit 0 of
 *   each channel control word (firmware file 0x9560: ldr r3,[chan+0xb0];
 *   ldr r2,[r3]; orr r2,#1; str r2,[r3]).  So the host never supplies them.
 */
/*
 * Read-only diagnostic dump of the register windows the gate lives in: the
 * ETE block's own control/status words (device CA 0x4003a000..0x4003a0ff),
 * every channel's +0x00/+0x08/+0x48, and the mailbox words.  Compared against
 * a live vendor boot (build/register-dumps/barmap_ep0_bar0.bin) this shows
 * exactly which enable the takeover lacks.  No writes.
 */
static void omo_dump_gate_regs(const char *tag)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i, k;

	for (k = 0; k < 0x40; k += 0x10) {
		pr_info("omo-hccaccept: DUMP %s ETE+%03x: %08x %08x %08x %08x\n",
			tag, k,
			ioread32(win + k), ioread32(win + k + 4),
			ioread32(win + k + 8), ioread32(win + k + 0xc));
	}
	for (i = 0; i < ETE_SR_N; i++)
		pr_info("omo-hccaccept: DUMP %s SR%u CA 0x%08x: +00=%08x +08=%08x +48=%08x\n",
			tag, i, 0x4003a000U + (u32)omo_sr_block[i],
			ioread32(win + omo_sr_block[i]),
			ioread32(win + omo_sr_block[i] + 0x08),
			ioread32(win + omo_sr_block[i] + 0x48));
	for (i = 0; i < ETE_DR_N; i++)
		pr_info("omo-hccaccept: DUMP %s DR%u CA 0x%08x: +00=%08x +08=%08x +48=%08x\n",
			tag, i + 3, 0x4003a000U + (u32)omo_dr_block[i],
			ioread32(win + omo_dr_block[i]),
			ioread32(win + omo_dr_block[i] + 0x08),
			ioread32(win + omo_dr_block[i] + 0x48));
	pr_info("omo-hccaccept: DUMP %s MBOX 39000=%08x 39010=%08x 39014=%08x 39108=%08x 3910c=%08x 39220=%08x 39224=%08x 392d0=%08x 392d4=%08x 392e8=%08x 392f0=%08x 392f4=%08x 101410=%08x 101430=%08x 101434=%08x\n",
		tag,
		ioread32(omo_bar0 + 0x3f1000), ioread32(omo_bar0 + 0x3f1010),
		ioread32(omo_bar0 + 0x3f1014), ioread32(omo_bar0 + 0x3f1108),
		ioread32(omo_bar0 + 0x3f110c), ioread32(omo_bar0 + 0x3f1220),
		ioread32(omo_bar0 + 0x3f1224), ioread32(omo_bar0 + 0x3f12d0),
		ioread32(omo_bar0 + 0x3f12d4), ioread32(omo_bar0 + 0x3f12e8),
		ioread32(omo_bar0 + 0x3f12f0), ioread32(omo_bar0 + 0x3f12f4),
		ioread32(omo_bar0 + 0x4b9410), ioread32(omo_bar0 + 0x4b9430),
		ioread32(omo_bar0 + 0x4b9434));
}

static void omo_set_enable(unsigned int mode)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i;
	u32 b0;

	/* Always log the pre-state of every gate candidate. */
	b0 = ioread32(omo_bar0 + omo_mbox[5].off);
	pr_info("omo-hccaccept: GATE pre out[5] CA=0x%08x = 0x%08x\n",
		omo_mbox[5].ca, b0);
	for (i = 0; i < ETE_SR_N; i++)
		pr_info("omo-hccaccept: GATE pre SR ch%u +0x00=0x%08x +0x48=0x%08x (CA 0x%08x)\n",
			i, ioread32(win + omo_sr_block[i]),
			ioread32(win + omo_sr_block[i] + 0x48),
			0x4003a000U + (u32)omo_sr_block[i]);
	for (i = 0; i < ETE_DR_N; i++)
		pr_info("omo-hccaccept: GATE pre DR ch%u +0x00=0x%08x +0x48=0x%08x (CA 0x%08x)\n",
			i + 3, ioread32(win + omo_dr_block[i]),
			ioread32(win + omo_dr_block[i] + 0x48),
			0x4003a000U + (u32)omo_dr_block[i]);

	if (mode == 0) {
		pr_info("omo-hccaccept: enable=0 - no gate write (control boot)\n");
		return;
	}

	if (mode & 1) {
		iowrite32(8, omo_bar0 + omo_mbox[5].off);
		pr_info("omo-hccaccept: ENABLE out[5] CA=0x%08x <= 0x00000008 readback=0x%08x (pcie_msg_send_irq @0x174a8: 0x17504 str r2=8,[out[5]])\n",
			omo_mbox[5].ca,
			ioread32(omo_bar0 + omo_mbox[5].off));
	}

	if (mode & 2) {
		for (i = 0; i < ETE_SR_N; i++) {
			unsigned long b = omo_sr_block[i];

			b0 = ioread32(win + b);
			iowrite32(b0 | 1U, win + b);
			pr_info("omo-hccaccept: ENABLE SR ch%u +0x00 0x%08x -> 0x%08x readback=0x%08x\n",
				i, b0, b0 | 1U, ioread32(win + b));
			b0 = ioread32(win + b + 0x48);
			iowrite32(1U, win + b + 0x48);
			pr_info("omo-hccaccept: ENABLE SR ch%u +0x48 0x%08x -> 0x00000001 readback=0x%08x\n",
				i, b0, ioread32(win + b + 0x48));
		}
		for (i = 0; i < ETE_DR_N; i++) {
			unsigned long b = omo_dr_block[i];

			b0 = ioread32(win + b);
			iowrite32(b0 | 1U, win + b);
			pr_info("omo-hccaccept: ENABLE DR ch%u +0x00 0x%08x -> 0x%08x readback=0x%08x\n",
				i + 3, b0, b0 | 1U, ioread32(win + b));
			b0 = ioread32(win + b + 0x48);
			iowrite32(1U, win + b + 0x48);
			pr_info("omo-hccaccept: ENABLE DR ch%u +0x48 0x%08x -> 0x00000001 readback=0x%08x\n",
				i + 3, b0, ioread32(win + b + 0x48));
		}
	}

	/*
	 * mode 4 - the firmware's own message-service enable bits.  The firmware's
	 * pcie_msg_init (file 0x9334, reached through the ops table) sets bit 0 of
	 * CA 0x40101410 and 0x40101430:
	 *   0x09794: add.w r2,r2,#0xc8000 ; add.w r2,r2,#0x3fc  ; r2 = 0x40101410
	 *   0x0979c: ldrh r3,[r2] ; orr r3,r3,#1 ; strh r3,[r2]
	 *   0x097f4: ldrh r3,[r2,#0x20] ; orr r3,r3,#1 ; strh r3,[r2,#0x20]
	 * A live vendor boot reads both as 1; the takeover reads both as 0 (see the
	 * DUMP lines), i.e. the firmware's message-service init never got that far.
	 * The host writing them is the quoted substitute for the missing enable.
	 */
	if (mode & 4) {
		b0 = ioread32(omo_bar0 + 0x4b9410);
		iowrite32(b0 | 1U, omo_bar0 + 0x4b9410);
		pr_info("omo-hccaccept: ENABLE 0x40101410 0x%08x -> 0x%08x readback=0x%08x (fw pcie_msg_init 0x097a0)\n",
			b0, b0 | 1U, ioread32(omo_bar0 + 0x4b9410));
		b0 = ioread32(omo_bar0 + 0x4b9430);
		iowrite32(b0 | 1U, omo_bar0 + 0x4b9430);
		pr_info("omo-hccaccept: ENABLE 0x40101430 0x%08x -> 0x%08x readback=0x%08x (fw pcie_msg_init 0x097f8)\n",
			b0, b0 | 1U, ioread32(omo_bar0 + 0x4b9430));
	}
}

static void omo_ete_wr(void __iomem *win, unsigned long off, u32 val,
		       const char *name)
{
	u32 rb;

	iowrite32(val, win + off);
	rb = ioread32(win + off);
	pr_info("omo-hccaccept:   %-22s [0x%03lx] <= 0x%08x readback=0x%08x match=%s\n",
		name, off, val, rb, rb == val ? "YES" : "NO");
}

static void omo_dump_full(const char *tag);

static void omo_ete_program(void)
{
	void __iomem *win;
	unsigned int i;

	if (!omo_ete_ready) {
		pr_warn("omo-hccaccept: rings=0 - no ETE programming\n");
		return;
	}

	win = omo_bar0 + ETE_BAR0_OFF;
	pr_info("omo-hccaccept: ETE block CA 0x4003a000 = BAR0+0x%lx (via region-3 viewport 0x403b8000->CA 0x40000000; the old phase-17 flat offset 0x3a000 = host 0x4003a000 reads 0x%08x, wrong region)\n",
		ETE_BAR0_OFF, ioread32(omo_bar0 + 0x3a000));

	/* Read-only pre-state. */
	pr_info("omo-hccaccept: ---- SR/DR program registers BEFORE ----\n");
	for (i = 0; i < ETE_SR_N; i++)
		pr_info("omo-hccaccept:   SR ch%u ctrl=0x%08x base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i, ioread32(win + omo_sr_block[i] + ETE_SR_CTRL),
			ioread32(win + omo_sr_block[i] + ETE_SR_BASE),
			ioread32(win + omo_sr_block[i] + ETE_SR_DEPTH),
			ioread32(win + omo_sr_block[i] + ETE_SR_WPTR));
	for (i = 0; i < ETE_DR_N; i++)
		pr_info("omo-hccaccept:   DR ch%u base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i + 3, ioread32(win + omo_dr_block[i] + ETE_DR_BASE),
			ioread32(win + omo_dr_block[i] + ETE_DR_DEPTH),
			ioread32(win + omo_dr_block[i] + ETE_DR_WPTR));

	/* Device-side binding write #1 (vendor order: before the rings).
	 * pcie_ete_intr_init @0x7528 (from pcie_ete_init): maps CA 0x40039508 and
	 * writes *p &= 0xffe0f8f8.  The earlier takeover never touched it. */
	{
		void __iomem *iw = omo_bar0 + ETE_INTR_OFF;
		u32 v = ioread32(iw);

		pr_info("omo-hccaccept: ---- pcie_ete_intr_init CA 0x40039508 pre=0x%08x mask=0x%08x ----\n",
			v, ETE_INTR_MASK);
		omo_ete_wr(iw, 0, v & ETE_INTR_MASK, "ETE intr 0x40039508");
	}

	/* SR: base, depth-1, wptr, ctrl (pcie_ete_sr_reg_init @0x14a48). */
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 devva = omo_hostca_to_devva(omo_sr_dma[i]) + (u32)omo_acpoff;
		char t[40];

		scnprintf(t, sizeof(t), "SR ch%u base", i);
		omo_ete_wr(win, b + ETE_SR_BASE, devva, t);
		scnprintf(t, sizeof(t), "SR ch%u depth-1", i);
		omo_ete_wr(win, b + ETE_SR_DEPTH, ETE_DEPTH - 1, t);
		scnprintf(t, sizeof(t), "SR ch%u wptr", i);
		omo_ete_wr(win, b + ETE_SR_WPTR, 0, t);
		scnprintf(t, sizeof(t), "SR ch%u ctrl", i);
		/* pcie_ete_sr_reg_init @0x14ae8 sets SR+0x08[2:0] = cfg[5]; the live
		 * vendor boot reads 0.  srctrl defaults to 1 (sr2 behaviour). */
		omo_ete_wr(win, b + ETE_SR_CTRL, omo_srctrl & 0x7u, t);
	}

	/* DR: base, depth-1, wptr (pcie_ete_dr_reg_init @0x1483c). */
	for (i = 0; i < ETE_DR_N; i++) {
		unsigned long b = omo_dr_block[i];
		u32 devva = omo_hostca_to_devva(omo_dr_dma[i]) + (u32)omo_acpoff;
		char t[40];

		scnprintf(t, sizeof(t), "DR ch%u base", i + 3);
		omo_ete_wr(win, b + ETE_DR_BASE, devva, t);
		scnprintf(t, sizeof(t), "DR ch%u depth-1", i + 3);
		omo_ete_wr(win, b + ETE_DR_DEPTH, ETE_DEPTH - 1, t);
		scnprintf(t, sizeof(t), "DR ch%u wptr", i + 3);
		omo_ete_wr(win, b + ETE_DR_WPTR, 0, t);
	}

	/* Device-side binding write #2 (vendor order: after the rings).
	 * pcie_ete_chn_res @0x7490 writes ONE register in the message/glue block,
	 * CA 0x400392e8 (= GLUE_BAR0_OFF+0x2e8): read, AND 0xfffffc20, write back.
	 * Captured live value 0x20.  The earlier takeover wrote this mask to the
	 * ETE ring block offsets instead, missing the real register. */
	{
		void __iomem *gw = omo_bar0 + GLUE_BAR0_OFF;
		u32 v = ioread32(gw + GLUE_CHN_RES);

		pr_info("omo-hccaccept: ---- pcie_ete_chn_res glue CA 0x400392e8 (mask 0x%08x) pre=0x%08x ----\n",
			GLUE_CHN_RES_MASK, v);
		omo_ete_wr(gw, GLUE_CHN_RES, v & GLUE_CHN_RES_MASK,
			"glue chn_res 0x400392e8");
	}

	pr_info("omo-hccaccept: ETE base values are the coherent DMA address + acpoff=%d (INFERRED: the vendor converts host CA -> device VA via the runtime window chip->[4]->[0xc4], pcie_hostca_to_devva @0xaefc)\n",
		omo_acpoff);
	pr_info("omo-hccaccept: ---- SR/DR program registers AFTER ----\n");
	for (i = 0; i < ETE_SR_N; i++)
		pr_info("omo-hccaccept:   SR ch%u ctrl=0x%08x base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i, ioread32(win + omo_sr_block[i] + ETE_SR_CTRL),
			ioread32(win + omo_sr_block[i] + ETE_SR_BASE),
			ioread32(win + omo_sr_block[i] + ETE_SR_DEPTH),
			ioread32(win + omo_sr_block[i] + ETE_SR_WPTR));
	for (i = 0; i < ETE_DR_N; i++)
		pr_info("omo-hccaccept:   DR ch%u base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i + 3, ioread32(win + omo_dr_block[i] + ETE_DR_BASE),
			ioread32(win + omo_dr_block[i] + ETE_DR_DEPTH),
			ioread32(win + omo_dr_block[i] + ETE_DR_WPTR));
	omo_dump_full("pre");
}

/* Field-by-field dump of all seven ETE channel blocks and the ETE interrupt
 * block.  Read-only; used at pre-release and post-release so a device rewrite
 * is visible. */
static void omo_dump_full(const char *tag)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	static const unsigned long blk[7] =
		{ 0x400, 0x450, 0x4a0, 0x590, 0x5e0, 0x630, 0x680 };
	unsigned int i;

	pr_info("omo-hccaccept: [%s] ETE intr 0x40039508=0x%08x +0c=0x%08x +10=0x%08x +14=0x%08x (vendor target 0x%08x)\n",
		tag, ioread32(omo_bar0 + ETE_INTR_OFF),
		ioread32(omo_bar0 + ETE_INTR_OFF + 4),
		ioread32(omo_bar0 + ETE_INTR_OFF + 8),
		ioread32(omo_bar0 + ETE_INTR_OFF + 0xc), ETE_INTR_VENDOR);
	for (i = 0; i < 7; i++) {
		unsigned long b = blk[i];

		pr_info("omo-hccaccept: [%s] blk 0x%03lx +00=%08x +04=%08x +08=%08x +0c=%08x +10=%08x +14=%08x +18=%08x +1c=%08x +20=%08x +24=%08x +28=%08x +2c=%08x +30=%08x +34=%08x +38=%08x +3c=%08x +40=%08x +44=%08x +48=%08x +4c=%08x\n",
			tag, b,
			(ioread32(win + b + 0x00)), (ioread32(win + b + 0x04)),
			(ioread32(win + b + 0x08)), (ioread32(win + b + 0x0c)),
			(ioread32(win + b + 0x10)), (ioread32(win + b + 0x14)),
			(ioread32(win + b + 0x18)), (ioread32(win + b + 0x1c)),
			(ioread32(win + b + 0x20)), (ioread32(win + b + 0x24)),
			(ioread32(win + b + 0x28)), (ioread32(win + b + 0x2c)),
			(ioread32(win + b + 0x30)), (ioread32(win + b + 0x34)),
			(ioread32(win + b + 0x38)), (ioread32(win + b + 0x3c)),
			(ioread32(win + b + 0x40)), (ioread32(win + b + 0x44)),
			(ioread32(win + b + 0x48)), (ioread32(win + b + 0x4c)));
	}
}

/* Part A trigger: the firmware's own pcie_msg_init reconfigures the ETE
 * interrupt block after the host's pre-release write.  Re-assert the quoted
 * vendor state post-release. */
static void omo_ete_intr_reassert(const char *tag)
{
	void __iomem *iw = omo_bar0 + ETE_INTR_OFF;
	u32 pre = ioread32(iw);
	u32 val = pre;

	if (omo_intr == 0) {
		pr_info("omo-hccaccept: [%s +%lums] ETE intr 0x40039508 mode=0 read-only pre=0x%08x\n",
			tag, omo_ms_now(), pre);
		return;
	}
	if (omo_intr == 1)
		val = ETE_INTR_VENDOR;
	else if (omo_intr == 2)
		val = pre | ETE_INTR_VENDOR;
	else if (omo_intr == 3)
		val = ETE_INTR_RESET;
	pr_info("omo-hccaccept: [%s +%lums] ETE intr 0x40039508 post-release pre=0x%08x mode=%u <= 0x%08x\n",
		tag, omo_ms_now(), pre, omo_intr, val);
	iowrite32(val, iw);
	pr_info("omo-hccaccept: [%s +%lums] ETE intr 0x40039508 readback=0x%08x (post-release re-assert)\n",
		tag, omo_ms_now(), ioread32(iw));
}

/* ---- IRQ ---------------------------------------------------------------- */

static void omo_decode_mailbox(const char *what, u32 v)
{
	int i;

	pr_info("omo-hccaccept:   %-18s = 0x%08x", what, v);
	if (!v) {
		pr_info("omo-hccaccept:     bits={-}\n");
		return;
	}
	for (i = 0; i < 16; i++)
		if (v & (1U << i))
			pr_info("omo-hccaccept:     bit %d (id %d = %s)\n",
				i, i, omo_msgid_name(i));
}

/* Returns IRQ_HANDLED for the first mailbox change, IRQ_NONE otherwise. */
static irqreturn_t omo_irq_handler(int irq, void *dev_id)
{
	u32 m[MAILBOX_N];
	unsigned int i, changed = 0;
	unsigned int second = (omo_dev2 && dev_id == (void *)omo_dev2);

	if (second)
		atomic_inc(&omo_irq2_count);
	else
		atomic_inc(&omo_irq_count);

	for (i = 0; i < MAILBOX_N; i++)
		m[i] = ioread32(omo_bar0 + omo_mbox[i].off);

	for (i = 0; i < MAILBOX_N; i++) {
		if (m[i] != omo_isr_seen[i]) {
			changed = 1;
			if (!second && omo_isr_logs < ISR_LOG_MAX) {
				omo_isr_logs++;
				pr_info("omo-hccaccept: [ISR +%lums] irq=%d %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
					omo_ms_now(), irq, omo_mbox[i].what,
					omo_mbox[i].ca, omo_isr_seen[i], m[i]);
				omo_decode_mailbox(omo_mbox[i].what, m[i]);
			} else if (second && omo_isr_logs2 < ISR_LOG_MAX) {
				omo_isr_logs2++;
				pr_info("omo-hccaccept: [ISR ep0 +%lums] irq=%d %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
					omo_ms_now(), irq, omo_mbox[i].what,
					omo_mbox[i].ca, omo_isr_seen[i], m[i]);
			}
			omo_isr_seen[i] = m[i];
		}
	}

	if (!changed)
		return IRQ_NONE;

	if (second)
		atomic_inc(&omo_irq2_hits);
	else
		atomic_inc(&omo_irq_hits);
	/* The host half: ack / clear / re-arm / dispatch (pcie_msg_handle). */
	omo_msg_service("isr");
	for (i = 0; i < MAILBOX_N; i++)
		omo_isr_seen[i] = ioread32(omo_bar0 + omo_mbox[i].off);

	/* If a line keeps firing after servicing it is not our source: take one
	 * and disable it so a shared level line cannot storm. */
	if (second) {
		if (atomic_read(&omo_irq2_count) > 64 && !omo_irq2_disabled) {
			omo_irq2_disabled = 1;
			disable_irq_nosync(irq);
		}
	} else if (atomic_read(&omo_irq_count) > 64 && !omo_irq_disabled) {
		omo_irq_disabled = 1;
		disable_irq_nosync(irq);
	}
	return IRQ_HANDLED;
}

static int omo_request_irq_line(void)
{
	u8 line = 0;
	int irq, rc;

	pci_read_config_byte(omo_dev, PCI_INTERRUPT_LINE, &line);
	pr_info("omo-hccaccept: INTx config before: PCI_INTERRUPT_LINE=%u (IRQ pin from cfg[0x3d])\n",
		line);
	/*
	 * The one unproven-but-quoted write: a takeover boot leaves
	 * PCI_INTERRUPT_LINE at 0xff; the live vendor boot has it at 0xcf
	 * (=207, the RC radm GIC-0 91 virq).  The vendor module never writes
	 * this byte (no cfg 0x3c store in plat.ko) - the platform writes it
	 * when a driver enables the device, which the takeover skipped.  Write
	 * the observed vendor value so the config space matches a vendor boot.
	 */
	if ((line == 0xff || line == 0) && omo_hostirq) {
		pci_write_config_byte(omo_dev, PCI_INTERRUPT_LINE,
				      (u8)omo_hostirq);
		pci_read_config_byte(omo_dev, PCI_INTERRUPT_LINE, &line);
		pr_info("omo-hccaccept: PCI_INTERRUPT_LINE <= %u (unproven-but-quoted, live vendor value) readback=%u\n",
			omo_hostirq, line);
	}
	irq = omo_irq ? omo_irq : line;
	pr_info("omo-hccaccept: INTx config: PCI_INTERRUPT_LINE=%u requested irq=%d\n",
		line, irq);
	if (irq <= 0) {
		pr_warn("omo-hccaccept: no usable IRQ line - polling only\n");
		return -EINVAL;
	}
	omo_irq_num = irq;
	memcpy(omo_isr_seen, omo_mbox_last, sizeof(omo_isr_seen));
	rc = request_irq(irq, omo_irq_handler, IRQF_SHARED, "omo-hccaccept", omo_dev);
	if (rc) {
		pr_err("omo-hccaccept: request_irq(%d, IRQF_SHARED) rc=%d - polling only\n",
		       irq, rc);
		omo_irq_num = -1;
		return rc;
	}
	omo_irq_ok = 1;
	pr_info("omo-hccaccept: request_irq(%d, IRQF_SHARED, \"omo-hccaccept\") rc=0 - IRQ path live (primary ep1)\n",
		irq);
	return 0;
}

/* Second endpoint (ep0, irq 207): the vendor requests both lines.  Its INTx
 * takes zero interrupts on a working vendor boot, but phase 20f proved the SR
 * fetch runs through this RC, so claim and request it for completeness. */
static int omo_request_irq2(void)
{
	u8 line = 0;
	int irq, rc;

	if (!omo_dev2)
		return -ENODEV;
	pci_read_config_byte(omo_dev2, PCI_INTERRUPT_LINE, &line);
	pr_info("omo-hccaccept: ep0 INTx config before: PCI_INTERRUPT_LINE=%u\n",
		line);
	if ((line == 0xff || line == 0) && omo_hostirq2) {
		pci_write_config_byte(omo_dev2, PCI_INTERRUPT_LINE,
				      (u8)omo_hostirq2);
		pci_read_config_byte(omo_dev2, PCI_INTERRUPT_LINE, &line);
		pr_info("omo-hccaccept: ep0 PCI_INTERRUPT_LINE <= %u (unproven-but-quoted) readback=%u\n",
			omo_hostirq2, line);
	}
	irq = omo_irq2 ? omo_irq2 : line;
	if (irq <= 0) {
		pr_warn("omo-hccaccept: ep0 no usable IRQ line - not requesting\n");
		return -EINVAL;
	}
	omo_irq2_num = irq;
	rc = request_irq(irq, omo_irq_handler, IRQF_SHARED, "omo-hccaccept-ep0",
			 omo_dev2);
	if (rc) {
		pr_err("omo-hccaccept: ep0 request_irq(%d, IRQF_SHARED) rc=%d\n", irq, rc);
		omo_irq2_num = -1;
		return rc;
	}
	omo_irq2_ok = 1;
	pr_info("omo-hccaccept: ep0 request_irq(%d, IRQF_SHARED, \"omo-hccaccept-ep0\") rc=0\n",
		irq);
	return 0;
}

static void omo_release_irq(void)
{
	if (!omo_irq_ok)
		goto second;
	if (omo_irq_disabled) {
		enable_irq(omo_irq_num);
		omo_irq_disabled = 0;
	}
	free_irq(omo_irq_num, omo_dev);
	omo_irq_ok = 0;
	pr_info("omo-hccaccept: freed primary ep1 irq %d (taken=%d handled=%d)\n",
		omo_irq_num, atomic_read(&omo_irq_count),
		atomic_read(&omo_irq_hits));
	omo_irq_num = -1;
second:
	if (!omo_irq2_ok)
		return;
	if (omo_irq2_disabled) {
		enable_irq(omo_irq2_num);
		omo_irq2_disabled = 0;
	}
	free_irq(omo_irq2_num, omo_dev2);
	omo_irq2_ok = 0;
	pr_info("omo-hccaccept: freed ep0 irq %d (taken=%d handled=%d)\n",
		omo_irq2_num, atomic_read(&omo_irq2_count),
		atomic_read(&omo_irq2_hits));
	omo_irq2_num = -1;
}

/* ---- polling ----------------------------------------------------------- */

static void omo_scan_changes(const char *tag)
{
	unsigned long off;
	unsigned int changed = 0, shown = 0;

	if (!omo_scan_base || !omo_scanlen)
		return;
	for (off = 0; off + 4 <= omo_scanlen; off += 4) {
		u32 w = ioread32(omo_bar0 + omo_scanbase + off);

		if (memcmp(&w, omo_scan_base + off, 4) != 0) {
			if (shown < SCAN_CHANGES_MAX) {
				u32 old;

				memcpy(&old, omo_scan_base + off, 4);
				pr_info("omo-hccaccept: [%s +%lums] SCAN CA=0x%08x BAR0+0x%lx 0x%08x -> 0x%08x\n",
					tag, omo_ms_now(),
					0x01320000U + (u32)off, omo_scanbase + off,
					old, w);
				shown++;
			}
			changed++;
		}
	}
	if (changed)
		pr_info("omo-hccaccept: [%s +%lums] scan window changed %u/%u words (shown %u)\n",
			tag, omo_ms_now(), changed, omo_scanlen / 4, shown);
}

/*
 * The vendor completion consumer: pcie_ete_transfer_done_handle @0x8730
 * (msg id 3) -> pcie_ete_d2h_isr_handle @0x15c1c(mask 0x1f) -> pcie_rx_handle
 * @0x164d0(ete, chn) -> pcie_ete_dr_get_uploadbuf @0x1515c ->
 * pcie_ete_rcv_buff_check @0x14d74.
 *
 * pcie_rx_handle reads the channel's hardware index (ldrh via the pointer the
 * vendor keeps at [drctx+0x34]) and compares it to the host's consumed index
 * [drctx+0x24], using the packed index's phase bit (0x400) to detect a wrap.
 * The device advances DR+0x3c; the host commits DR+0x38.  We have no BAL rx
 * callback, so the faithful observable action is: read both index registers,
 * scan our node arrays and payload buffers, and test the buffer header the
 * consumer tests (ldrh [buf+0xa] == 0x5a5a with ldrh [buf+4] != 0 ->
 * pcie_ete_rcv_buff_check @0x14df0..0x14dfc).
 */
static void omo_scan_dr(const char *tag)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i;
	unsigned long off;
	unsigned int hits = 0;

	if (!omo_ete_ready)
		return;
	for (i = 0; i < ETE_DR_N; i++) {
		unsigned long b = omo_dr_block[i];
		u32 wptr = ioread32(win + b + ETE_DR_WPTR);
		u32 rptr = ioread32(win + b + ETE_DR_RPTR);
		u8 *n = omo_dr_va[i];
		u8 *p = omo_dr_pay[i];
		u16 magic = 0;
		u16 hlen = 0;

		if (rptr != omo_dr_rptr_last[i]) {
			pr_info("omo-hccaccept: [%s +%lums] DR ch%u DEVICE INDEX 0x%08x -> 0x%08x (host wptr=0x%08x, delta=%u) = pcie_rx_handle saw a completion\n",
				tag, omo_ms_now(), i + 3,
				omo_dr_rptr_last[i], rptr, wptr,
				(rptr - omo_dr_rptr_last[i]) & 0x3ff);
			omo_dr_rptr_last[i] = rptr;
			omo_dr_events++;
		}
		for (off = 0; off + 8 <= ETE_DEPTH * 8; off += 8) {
			if (memcmp(n + off, omo_dr_snap[i] + off, 8) != 0) {
				u32 w0, w1;

				memcpy(&w0, n + off, 4);
				memcpy(&w1, n + off + 4, 4);
				pr_info("omo-hccaccept: [%s +%lums] DR ch%u node[%lu] CHANGED addr=0x%08x ctl=0x%08x (len=%u flag=0x%03x owner13=%u owner14=%u)\n",
					tag, omo_ms_now(), i + 3, off / 8, w0, w1,
					w1 >> 16, w1 & 0x1fff,
					(w1 >> 13) & 1, (w1 >> 14) & 1);
				memcpy(omo_dr_snap[i] + off, n + off, 8);
				hits++;
			}
		}
		for (off = 0; off + 4 <= ETE_DR_PAYLOAD; off += 4) {
			if (memcmp(p + off, omo_pay_snap[i] + off, 4) != 0) {
				u32 w;

				memcpy(&w, p + off, 4);
				pr_info("omo-hccaccept: [%s +%lums] DR ch%u payload+0x%03lx = 0x%08x\n",
					tag, omo_ms_now(), i + 3, off, w);
				memcpy(omo_pay_snap[i] + off, p + off, 4);
				hits++;
			}
		}
		/* the exact test pcie_ete_rcv_buff_check @0x14df0 makes */
		memcpy(&magic, p + 0xa, 2);
		memcpy(&hlen, p + 4, 2);
		if (magic == 0x5a5a) {
			if (!omo_pay_magic[i]) {
				omo_pay_magic[i] = 1;
				omo_dr_events++;
				pr_info("omo-hccaccept: [%s +%lums] DR ch%u BUFFER 0x5a5a MAGIC at +0xa, hdr+4=0x%04x (rcv_buff_check @0x14d74)\n",
					tag, omo_ms_now(), i + 3, hlen);
			}
		}
	}
	if (hits)
		pr_info("omo-hccaccept: [%s +%lums] DR/payload changes: %u (device-index/magic events=%u)\n",
			tag, omo_ms_now(), hits, omo_dr_events);
}

/*
 * The SR consumer side: the vendor's pcie_ete_sending_trigger @0x13f90 runs
 * from the SR pump (pcie_thread_handle @0x16b00) after the device's id-6 wake
 * (pcie_trigger_ete_sending_handle -> pcie_wkup_thread).  It reads the SR
 * channel index, walks the nodes and recognises the device's message by the
 * 0x5a5a header magic the HCC layer writes at buffer+0xa (hcc_msg_process
 * @0x1204c reads byte[0]&0xf as group and u16@+6 as the HCC message id).
 * There is no BAL rx callback in the takeover, so the faithful observable
 * action is: read both SR index registers, scan our node arrays and message
 * buffers, and decode any HCC frame that appears.
 */
static void omo_scan_sr(const char *tag)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i;
	unsigned long off;
	unsigned int hits = 0;

	if (!omo_ete_ready)
		return;
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 wptr = ioread32(win + b + ETE_SR_WPTR);
		u32 rptr = ioread32(win + b + ETE_SR_RPTR);
		u8 *n = omo_sr_va[i];
		u8 *p = omo_sr_pay[i];
		unsigned int chits = 0;

		if (rptr != omo_sr_rptr_last[i]) {
			pr_info("omo-hccaccept: [%s +%lums] SR ch%u DEVICE INDEX 0x%08x -> 0x%08x (host wptr=0x%08x) = SR engine read\n",
				tag, omo_ms_now(), i, omo_sr_rptr_last[i], rptr, wptr);
			omo_sr_rptr_last[i] = rptr;
			omo_sr_events++;
		}
		for (off = 0; off + 8 <= ETE_DEPTH * 8; off += 8) {
			if (memcmp(n + off, omo_sr_snap[i] + off, 8) != 0) {
				u32 w0, w1;

				memcpy(&w0, n + off, 4);
				memcpy(&w1, n + off + 4, 4);
				pr_info("omo-hccaccept: [%s +%lums] SR ch%u node[%lu] CHANGED addr=0x%08x ctl=0x%08x (len=%u flag=0x%03x owner13=%u owner14=%u)\n",
					tag, omo_ms_now(), i, off / 8, w0, w1,
					w1 >> 16, w1 & 0x1fff,
					(w1 >> 13) & 1, (w1 >> 14) & 1);
				memcpy(omo_sr_snap[i] + off, n + off, 8);
				hits++;
				chits++;
			}
		}
		for (off = 0; off + 4 <= ETE_DEPTH * ETE_SR_PAYLOAD; off += 4) {
			if (memcmp(p + off, omo_sr_pay_snap[i] + off, 4) != 0) {
				u32 w;

				memcpy(&w, p + off, 4);
				pr_info("omo-hccaccept: [%s +%lums] SR ch%u payload+0x%04lx = 0x%08x\n",
					tag, omo_ms_now(), i, off, w);
				memcpy(omo_sr_pay_snap[i] + off, p + off, 4);
				hits++;
				chits++;
			}
		}
		/*
		 * Decode an HCC frame only after the device actually touched this
		 * channel (our own post produced no diff), using the vendor
		 * consumer's test: magic 0x5a5a at +0xa, id at +6, group byte[0].
		 */
		if (chits) {
			u16 magic, id, plen;
			u8 grp;

			memcpy(&magic, p + 0xa, 2);
			memcpy(&id, p + 6, 2);
			memcpy(&plen, p + 4, 2);
			grp = p[0] & 0x0f;
			if (magic == 0x5a5a && !omo_sr_msg_seen[i]) {
				omo_sr_msg_seen[i] = 1;
				omo_sr_events++;
				pr_info("omo-hccaccept: [%s +%lums] SR ch%u HCC MESSAGE slot0: magic=0x5a5a id=%u group=%u len=%u (hcc_msg_process @0x1204c)%s\n",
					tag, omo_ms_now(), i, id, grp, plen,
					id == 1 ? " -- ID-1 READY!" : "");
			}
		}
	}
	if (hits)
		pr_info("omo-hccaccept: [%s +%lums] SR/payload changes: %u (sr_events=%u)\n",
			tag, omo_ms_now(), hits, omo_sr_events);
}

/* pcie_ete_transfer_done_handle @0x8730: id 3 with non-NULL arg tail-calls
 * pcie_ete_d2h_isr_handle(ete, 0x1f, 0), which walks [ete+0x1c]
 * (= pcie_rx_handle, installed by pcie_ete_intr_init @0x7528) for every set
 * bit and calls pcie_rx_handle(ete, chn) for DR channels 3..6.  We run our
 * DR consumer scan instead of the missing BAL cbs->rx callback. */
static void omo_rx_handler(void *arg)
{
	pr_info("omo-hccaccept: [id3] pcie_ete_transfer_done_handle @0x8730 arg=%px -> pcie_ete_d2h_isr_handle('ete',0x1f,0) -> pcie_rx_handle @0x164d0 (DR ch3..6); pcie_ete_dr_get_uploadbuf @0x1515c\n",
		arg);
	omo_scan_dr("id3");
}

static void omo_poll_mailbox(const char *tag, unsigned int first)
{
	unsigned int i;

	for (i = 0; i < MAILBOX_N; i++) {
		u32 v = ioread32(omo_bar0 + omo_mbox[i].off);

		if (v != omo_mbox_last[i] || first) {
			pr_info("omo-hccaccept: [%s +%lums] MBOX %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
				tag, omo_ms_now(), omo_mbox[i].what,
				omo_mbox[i].ca, omo_mbox_last[i], v);
			omo_decode_mailbox(omo_mbox[i].what, v);
			if (!first && v)
				omo_msgs++;
			omo_mbox_last[i] = v;
		}
	}
	for (i = 0; i < STATUS_N; i++) {
		u32 v = ioread32(omo_bar0 + omo_stat[i].off);

		if (v != omo_stat_last[i]) {
			pr_info("omo-hccaccept: [%s +%lums] STAT %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
				tag, omo_ms_now(), omo_stat[i].what,
				omo_stat[i].ca, omo_stat_last[i], v);
			omo_stat_last[i] = v;
		}
	}
}

static void omo_snapshot_all(void)
{
	unsigned int i;
	unsigned long off;

	for (i = 0; i < MAILBOX_N; i++)
		omo_mbox_last[i] = ioread32(omo_bar0 + omo_mbox[i].off);
	for (i = 0; i < STATUS_N; i++)
		omo_stat_last[i] = ioread32(omo_bar0 + omo_stat[i].off);
	if (omo_scan_base && omo_scanlen)
		for (off = 0; off + 4 <= omo_scanlen; off += 4) {
			u32 w = ioread32(omo_bar0 + omo_scanbase + off);

			memcpy(omo_scan_base + off, &w, 4);
		}
	if (omo_ete_ready) {
		for (i = 0; i < ETE_SR_N; i++) {
			memcpy(omo_sr_snap[i], omo_sr_va[i], ETE_DEPTH * 8);
			memcpy(omo_sr_pay_snap[i], omo_sr_pay[i],
			       ETE_DEPTH * ETE_SR_PAYLOAD);
			omo_sr_rptr_last[i] = ioread32(omo_bar0 + ETE_BAR0_OFF +
				omo_sr_block[i] + ETE_SR_RPTR);
		}
		for (i = 0; i < ETE_DR_N; i++) {
			memcpy(omo_dr_snap[i], omo_dr_va[i], ETE_DEPTH * 8);
			memcpy(omo_pay_snap[i], omo_dr_pay[i], ETE_DR_PAYLOAD);
		}
	}
}

/*
 * The ETE glue status service (pcie_intr_handle @0x82e4 / oal_pcie_transfer_done
 * @0x83e4).  pcie_intr_handle reads [[ctx+4]]+0x2ec, dsb, masks 0x3d8 and
 * dispatches the lowest set bit through a handler table; oal_pcie_transfer_done
 * clears the consumed DMA-completion bits by OR-ing them into the glue word
 * (write-1-to-clear).  With no ETE priv pointer in the takeover the reachable
 * register is the ETE block's own +0x2ec; read it, and W1C the 0x3d8 bits when
 * set.  No write happens while it reads 0.
 */
static void omo_glue_service(const char *tag)
{
	void __iomem *win = omo_bar0 + GLUE_BAR0_OFF;
	u32 st = ioread32(win + GLUE_STAT);

	/* pcie_intr_handle @0x82e4 reads [[ctx+4]]+0x2ec; captured live the base
	 * is the message/glue block CA 0x40039000, so the status word is
	 * CA 0x400392ec.  The vendor only READS it (the host ack/re-arm writes at
	 * out[3]/out[4] let the device clear it), so keep this read-only and log
	 * every change plus the masked pending bits. */
	if (st != omo_glue_last) {
		pr_info("omo-hccaccept: [glue %s +%lums] CA 0x400392ec (BAR0+0x%lx) 0x%08x -> 0x%08x mask0x3d8=0x%08x (pcie_intr_handle @0x82e4)\n",
			tag, omo_ms_now(), (unsigned long)(GLUE_BAR0_OFF + GLUE_STAT),
			omo_glue_last, st, st & GLUE_STAT_MASK);
		omo_glue_last = st;
	}
}

/* Every state change we care about: out[0]/out[1] and the SR/DR indices. */
static void omo_svc_log_state(const char *tag)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	u32 o0 = ioread32(omo_bar0 + omo_mbox[0].off);
	u32 o1 = ioread32(omo_bar0 + omo_mbox[1].off);
	unsigned int i;

	if (o0 != omo_out0_last) {
		pr_info("omo-hccaccept: [%s +%lums] out[0] CA=0x%08x 0x%08x -> 0x%08x%s\n",
			tag, omo_ms_now(), omo_mbox[0].ca, omo_out0_last, o0,
			(o0 == 0 && omo_out0_last) ? " -- H2D MASK CLEARED BY DEVICE" : "");
		omo_out0_last = o0;
	}
	if (o1 != omo_out1_last) {
		pr_info("omo-hccaccept: [%s +%lums] out[1] CA=0x%08x 0x%08x -> 0x%08x\n",
			tag, omo_ms_now(), omo_mbox[1].ca, omo_out1_last, o1);
		omo_out1_last = o1;
	}
	for (i = 0; i < ETE_SR_N; i++) {
		u32 w = ioread32(win + omo_sr_block[i] + ETE_SR_WPTR);
		u32 r = ioread32(win + omo_sr_block[i] + ETE_SR_RPTR);
		u32 base = ioread32(win + omo_sr_block[i] + ETE_SR_BASE);
		u32 ctrl = ioread32(win + omo_sr_block[i] + ETE_SR_CTRL);

		if (base != omo_sr_base_last[i]) {
			pr_info("omo-hccaccept: [%s +%lums] SR ch%u BLOCK base(+0x10) 0x%08x -> 0x%08x ctrl(+0x08)=0x%08x\n",
				tag, omo_ms_now(), i, omo_sr_base_last[i], base, ctrl);
			omo_sr_base_last[i] = base;
		}
		if (w != omo_sr_idx_last[i] || r != omo_sr_pcs_last[i]) {
			pr_info("omo-hccaccept: [%s +%lums] SR ch%u wptr(+0x18)=0x%08x rptr(+0x1c)=0x%08x base(+0x10)=0x%08x\n",
				tag, omo_ms_now(), i, w, r, base);
			omo_sr_idx_last[i] = w;
			omo_sr_pcs_last[i] = r;
		}
	}
	for (i = 0; i < ETE_DR_N; i++) {
		u32 w = ioread32(win + omo_dr_block[i] + ETE_DR_WPTR);
		u32 r = ioread32(win + omo_dr_block[i] + ETE_DR_RPTR);

		if (r != omo_dr_idx_last[i]) {
			pr_info("omo-hccaccept: [%s +%lums] DR ch%u wptr(+0x38)=0x%08x rptr(+0x3c)=0x%08x\n",
				tag, omo_ms_now(), i + 3, w, r);
			omo_dr_idx_last[i] = r;
		}
	}
}

/*
 * pcie_process_thread @0x16efc: the vendor kthread waits on
 * pcie_wait_condtion(comm+0x28) (set by pcie_wkup_thread @0x1629c) and runs
 * pcie_thread_handle @0x16b00 each wake: release TX buffers, pump the SR ring
 * (pcie_ete_sending_trigger @0x13f90), consume the DR ring
 * (pcie_ete_tx_queue_handle @0x155d4) and dispatch completions.  The takeover
 * has no BAL callbacks, so this kthread runs the device-visible subset: the
 * ETE glue status clear, the DR/SR completion/index scan, the host message
 * dispatch (pcie_msg_handle @0x171f8) and the periodic doorbell re-ring
 * (pcie_msg_send @0x160f4), logging every state change.
 */
static int omo_svc_thread(void *arg)
{
	unsigned long t0 = jiffies;
	unsigned long dur = msecs_to_jiffies(omo_svcdur);

	(void)arg;
	pr_info("omo-hccaccept: service thread up (pcie_process_thread @0x16efc emulation) dur=%ums interval=%ums doorbell=%u\n",
		omo_svcdur, omo_svcms, omo_svcdoorbell);
	while (!kthread_should_stop() && time_before(jiffies, t0 + dur)) {
		msleep(omo_svcms);
		omo_svc_iters++;
		omo_poll_mailbox("svc", 0);
		omo_msg_service("svc");	/* pcie_msg_handle: out[1] */
		omo_glue_service("svc");	/* pcie_intr_handle / transfer_done */
		omo_scan_dr("svc");		/* completion handling */
		omo_scan_sr("svc");		/* SR ring consumer side */
		omo_pump_sr("svc");		/* SR producer: refill + commit SR+0x18 + id-3 */
		omo_svc_log_state("svc");
		if ((omo_svc_iters % omo_svcdoorbell) == 0) {
			omo_post_dr();		/* re-post DR buffers */
			omo_send_doorbell("svc", MSG_SEND_ID);
			omo_send_doorbell("svc", MSG_RECLAIM_ID);
		}
	}
	pr_info("omo-hccaccept: service thread exit after %u iters (glue_clears=%u)\n",
		omo_svc_iters, omo_glue_clears);
	complete(&omo_svc_done);
	return 0;
}

static int omo_do_release(void)
{
	u32 rb;

	pr_info("omo-hccaccept: RELEASE write CA 0x40000108 <- 0x%08x (BAR0+0x%lx)\n",
		RELEASE_VAL, RELEASE_OFF);
	iowrite32(RELEASE_VAL, omo_bar0 + RELEASE_OFF);
	rb = ioread32(omo_bar0 + RELEASE_OFF);
	pr_info("omo-hccaccept: release readback = 0x%08x\n", rb);
	return rb == RELEASE_VAL ? 0 : -EIO;
}

/* ======================================================================
 * phase-22 hccaccept additions: the batch convention (docs/phase22/
 * exp-harness.md) and the H2/H3/H4 + out[5] hypotheses aimed at the
 * device-side H2D HCC accept gate (out[0] CA 0x40039010 never cleared, no
 * id-1 reply).  Base configuration is the proven bothep boot 2: ep0 primary
 * (domain=0, irq=207), ep1 secondary (domain2=1, irq2=209), both RCs decoded,
 * rings/release through ep0's BAR, ISR live on 209.
 *
 * Hypothesis selector (param hyp=, per-entry override in batch mode):
 *   none      control: refill + full-lap SR commit + id-3 doorbell
 *   glue      H4: CA 0x400392e8 <= 0x20 (vendor idle value), read 0x400392ec
 *   intror    H2: CA 0x40039508 <= 0x3f201f1f (per-channel bits 0-2,8-10)
 *   intrbit   H2: CA 0x40039508 <= 0x3f201818 | (1 << arg)
 *   h3noop    H3: SR ch0 +0x18 <= rptr (no-op edge)
 *   h3full    H3: SR ch0 +0x18 <= rptr + depth (full lap, phase toggle)
 *   h3zero    H3: SR ch0 +0x18 <= 0x000 then 0x400 (explicit phase toggle)
 *   h3ctrl    H3: SR ch0 +0x08 low3 <= arg, then full-lap commit
 *   h3ctrl48  H3: SR ch0 +0x48 <= arg, then full-lap commit
 *   out5      BOOT B: full-lap commit, wait for fetch, CA 0x400392f0 <= 8
 * ====================================================================== */

#define OMOH_LABEL_MAX 48
#define OMOH_PARAM_MAX 160

struct omo_hyp_result {
	char label[OMOH_LABEL_MAX];
	char params[OMOH_PARAM_MAX];
	char out0;		/* 'y' / 'n' / '?' */
	int  id1;		/* an id-1 reply or payload was observed */
	int  fetched;		/* SR ch0 consumer advanced during the window */
	u32  sr1c;
	u32  glue;
	unsigned int irq;
};

static u32 omo_base_intr;
static u32 omo_base_glue_e8;
static u32 omo_base_sr_ctrl[ETE_SR_N];
static u32 omo_entry_rptr0;
static int omo_ctrl_fetched;

/* --- tiny kernel file helpers (result.txt / batch.done) ---------------- */

static int omo_mkdir(const char *path)
{
	struct path p;
	struct dentry *d;
	int rc;

	d = kern_path_create(AT_FDCWD, path, &p, 0);
	if (IS_ERR(d))
		return PTR_ERR(d);
	rc = vfs_mkdir(d_inode(p.dentry), d, 0755);
	done_path_create(&p, d);
	if (rc == -EEXIST)
		rc = 0;
	return rc;
}

static int omo_write_file(const char *path, const char *data, size_t len)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;

	f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f)) {
		pr_warn("omo-hccaccept: filp_open(%s) failed %ld\n",
			path, PTR_ERR(f));
		return PTR_ERR(f);
	}
	n = kernel_write(f, data, len, &pos);
	filp_close(f, NULL);
	return n < 0 ? (int)n : 0;
}

/* --- SR ring helpers --------------------------------------------------- */

static u32 omo_sr_rptr(unsigned int i)
{
	return ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[i] +
			ETE_SR_RPTR);
}

static u32 omo_sr_wptr(unsigned int i)
{
	return ioread32(omo_bar0 + ETE_BAR0_OFF + omo_sr_block[i] +
			ETE_SR_WPTR);
}

static void omo_sr_set_wptr(unsigned int i, u32 v)
{
	iowrite32(v, omo_bar0 + ETE_BAR0_OFF + omo_sr_block[i] + ETE_SR_WPTR);
}

static void omo_refill_sr_ch(unsigned int i)
{
	u64 *n = omo_sr_va[i];
	unsigned int j;

	for (j = 0; j < ETE_DEPTH; j++) {
		u32 devva = omo_hostca_to_devva(omo_sr_pay_dma[i]) +
			(u32)omo_acpoff + j * ETE_SR_PAYLOAD;
		u32 ln = (j == 1) ? ETE_SR_ALG_LEN : ETE_SR_MSG_LEN;
		u64 w1 = (u64)(u32)((ln << 16) | ETE_SR_FLAG);

		n[j] = (w1 << 32) | devva;
	}
}

/* --- post-release base state + per-entry reset ------------------------- */

static void omo_record_base(void)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	unsigned int i;

	omo_base_intr = ioread32(omo_bar0 + ETE_INTR_OFF);
	omo_base_glue_e8 = ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_CHN_RES);
	for (i = 0; i < ETE_SR_N; i++)
		omo_base_sr_ctrl[i] = ioread32(win + omo_sr_block[i] + ETE_SR_CTRL);
	pr_info("omo-hccaccept: base recorded: ETE intr 0x40039508=0x%08x glue 0x400392e8=0x%08x sr_ctrl=%08x/%08x/%08x\n",
		omo_base_intr, omo_base_glue_e8, omo_base_sr_ctrl[0],
		omo_base_sr_ctrl[1], omo_base_sr_ctrl[2]);
}

/* Restore the base device state the module saw right after firmware boot:
 * the ETE interrupt block, the glue chn_res word, the SR base/depth/ctrl, a
 * fresh node fill and wptr := rptr (no commit).  This is the "same reset as
 * probe start" the batch convention requires. */
static void omo_hyp_base_reset(void)
{
	unsigned int i;

	iowrite32(omo_base_intr, omo_bar0 + ETE_INTR_OFF);
	iowrite32(omo_base_glue_e8, omo_bar0 + GLUE_BAR0_OFF + GLUE_CHN_RES);
	iowrite32(0, omo_bar0 + omo_mbox[0].off);
	iowrite32(0, omo_bar0 + omo_mbox[1].off);
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 want = omo_hostca_to_devva(omo_sr_dma[i]) + (u32)omo_acpoff;
		u32 r;

		omo_refill_sr_ch(i);
		iowrite32(want, omo_bar0 + ETE_BAR0_OFF + b + ETE_SR_BASE);
		iowrite32(ETE_DEPTH - 1,
			  omo_bar0 + ETE_BAR0_OFF + b + ETE_SR_DEPTH);
		iowrite32(omo_base_sr_ctrl[i],
			  omo_bar0 + ETE_BAR0_OFF + b + ETE_SR_CTRL);
		r = omo_sr_rptr(i);
		iowrite32(r, omo_bar0 + ETE_BAR0_OFF + b + ETE_SR_WPTR);
	}
	omo_entry_rptr0 = omo_sr_rptr(0);
	omo_post_dr();
}

/* A full-lap commit on all three SR channels + the id-3 doorbell (the pump's
 * default action), used by the control and the H2/H4 register hypotheses. */
static void omo_hyp_commit_lap(void)
{
	unsigned int i;

	for (i = 0; i < ETE_SR_N; i++) {
		u32 idx = omo_sr_wptr(i);
		unsigned int k;

		for (k = 0; k < ETE_DEPTH; k++)
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
		omo_sr_set_wptr(i, idx);
	}
	omo_send_doorbell("hyp", MSG_SEND_ID);
}

static void omo_hyp_apply(const char *hyp, unsigned int arg)
{
	void __iomem *win = omo_bar0 + ETE_BAR0_OFF;
	u32 v, c, idx;
	unsigned int k;

	if (!strcmp(hyp, "glue")) {
		u32 pre = ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_CHN_RES);
		u32 pre_st = ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_STAT);

		iowrite32(0x00000020U,
			  omo_bar0 + GLUE_BAR0_OFF + GLUE_CHN_RES);
		pr_info("omo-hccaccept: [hyp glue] CA 0x400392e8 0x%08x -> 0x00000020 readback=0x%08x; status 0x400392ec 0x%08x -> 0x%08x (H4)\n",
			pre, ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_CHN_RES),
			pre_st, ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_STAT));
		omo_hyp_commit_lap();
		return;
	}
	if (!strcmp(hyp, "intror")) {
		v = ioread32(omo_bar0 + ETE_INTR_OFF);
		iowrite32(0x3f201f1fU, omo_bar0 + ETE_INTR_OFF);
		pr_info("omo-hccaccept: [hyp intror] CA 0x40039508 0x%08x -> 0x3f201f1f readback=0x%08x (H2)\n",
			v, ioread32(omo_bar0 + ETE_INTR_OFF));
		omo_hyp_commit_lap();
		return;
	}
	if (!strcmp(hyp, "intrbit")) {
		v = ioread32(omo_bar0 + ETE_INTR_OFF);
		iowrite32(0x3f201818U | (1U << arg), omo_bar0 + ETE_INTR_OFF);
		pr_info("omo-hccaccept: [hyp intrbit%u] CA 0x40039508 0x%08x -> 0x%08x readback=0x%08x (H2)\n",
			arg, v, 0x3f201818U | (1U << arg),
			ioread32(omo_bar0 + ETE_INTR_OFF));
		omo_hyp_commit_lap();
		return;
	}
	if (!strcmp(hyp, "h3noop")) {
		u32 rp = omo_sr_rptr(0);

		omo_sr_set_wptr(0, rp);
		pr_info("omo-hccaccept: [hyp h3noop] SR ch0 +0x18 <= rptr 0x%08x (H3)\n", rp);
		omo_send_doorbell("hyp", MSG_SEND_ID);
		return;
	}
	if (!strcmp(hyp, "h3full")) {
		idx = omo_sr_rptr(0);
		for (k = 0; k < ETE_DEPTH; k++)
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
		omo_sr_set_wptr(0, idx);
		pr_info("omo-hccaccept: [hyp h3full] SR ch0 +0x18 <= rptr+depth 0x%08x (H3)\n",
			idx);
		omo_send_doorbell("hyp", MSG_SEND_ID);
		return;
	}
	if (!strcmp(hyp, "h3zero")) {
		omo_sr_set_wptr(0, 0);
		omo_sr_set_wptr(0, 0x400);
		pr_info("omo-hccaccept: [hyp h3zero] SR ch0 +0x18 <= 0x00000000 then 0x00000400 (H3)\n");
		omo_send_doorbell("hyp", MSG_SEND_ID);
		return;
	}
	if (!strcmp(hyp, "h3ctrl")) {
		c = ioread32(win + omo_sr_block[0] + ETE_SR_CTRL);
		iowrite32((c & ~0x7U) | (arg & 0x7U),
			  win + omo_sr_block[0] + ETE_SR_CTRL);
		idx = omo_sr_rptr(0);
		for (k = 0; k < ETE_DEPTH; k++)
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
		omo_sr_set_wptr(0, idx);
		pr_info("omo-hccaccept: [hyp h3ctrl%u] SR ch0 +0x08 0x%08x -> 0x%08x, commit 0x%08x (H3)\n",
			arg, c, ioread32(win + omo_sr_block[0] + ETE_SR_CTRL), idx);
		omo_send_doorbell("hyp", MSG_SEND_ID);
		return;
	}
	if (!strcmp(hyp, "h3ctrl48")) {
		c = ioread32(win + omo_sr_block[0] + 0x48);
		iowrite32(arg, win + omo_sr_block[0] + 0x48);
		idx = omo_sr_rptr(0);
		for (k = 0; k < ETE_DEPTH; k++)
			idx = omo_ring_ptr_plus(idx, ETE_DEPTH);
		omo_sr_set_wptr(0, idx);
		pr_info("omo-hccaccept: [hyp h3ctrl48%u] SR ch0 +0x48 0x%08x -> 0x%08x, commit 0x%08x (H3)\n",
			arg, c, ioread32(win + omo_sr_block[0] + 0x48), idx);
		omo_send_doorbell("hyp", MSG_SEND_ID);
		return;
	}
	if (!strcmp(hyp, "out5")) {
		u32 r0;
		unsigned int w;

		omo_hyp_commit_lap();
		r0 = omo_sr_rptr(0);
		for (w = 0; w < 30; w++) {
			msleep(100);
			if (omo_sr_rptr(0) != r0)
				break;
		}
		pr_info("omo-hccaccept: [hyp out5] pre-arm out[5]=0x%08x sr1c=0x%08x out0=0x%08x out1=0x%08x (pcie_msg_send_irq @0x174a8)\n",
			ioread32(omo_bar0 + omo_mbox[5].off), omo_sr_rptr(0),
			ioread32(omo_bar0 + omo_mbox[0].off),
			ioread32(omo_bar0 + omo_mbox[1].off));
		/* persistent marker immediately before the risky write, so a watchdog
		 * reset distinguishes "hung at the out[5] arm" from an earlier stop */
		if (omo_resultpath && omo_resultpath[0]) {
			char mb[192];
			int mbl = scnprintf(mb, sizeof(mb),
				"stage=arming-out5 sr1c=0x%08x out0=0x%08x out1=0x%08x irq=%u\n",
				omo_sr_rptr(0),
				ioread32(omo_bar0 + omo_mbox[0].off),
				ioread32(omo_bar0 + omo_mbox[1].off),
				max_t(unsigned int, atomic_read(&omo_irq_count),
				      atomic_read(&omo_irq2_count)));

			omo_write_file(omo_resultpath, mb, mbl);
		}
		iowrite32(8, omo_bar0 + omo_mbox[5].off);
		pr_info("omo-hccaccept: [hyp out5] CA 0x400392f0 <= 0x00000008 readback=0x%08x\n",
			ioread32(omo_bar0 + omo_mbox[5].off));
		return;
	}
	pr_info("omo-hccaccept: [hyp %s] control/default - full-lap SR commit + id-3 doorbell\n",
		hyp);
	omo_hyp_commit_lap();
}

/* --- per-entry observables --------------------------------------------- */

static void omo_sample_entry(struct omo_hyp_result *res, unsigned int winms)
{
	u32 prev0 = ioread32(omo_bar0 + omo_mbox[0].off);
	unsigned int elapsed = 0;
	int cleared = 0, id1 = 0;

	while (elapsed < winms) {
		u32 o0, o1;
		unsigned int i;

		msleep(50);
		elapsed += 50;
		o0 = ioread32(omo_bar0 + omo_mbox[0].off);
		if (prev0 && !o0)
			cleared = 1;
		prev0 = o0;
		o1 = ioread32(omo_bar0 + omo_mbox[1].off);
		if (o1 & 0x2U)
			id1 = 1;
		for (i = 0; i < ETE_DR_N; i++) {
			u8 *p = omo_dr_pay[i];
			u16 magic = 0, id = 0;

			memcpy(&magic, p + 0xa, 2);
			if (magic != 0x5a5a)
				continue;
			memcpy(&id, p + 6, 2);
			if (id == 1)
				id1 = 1;
		}
	}
	res->out0 = cleared ? 'y' : 'n';
	res->id1 = id1;
	res->fetched = (omo_sr_rptr(0) != omo_entry_rptr0);
	res->sr1c = omo_sr_rptr(0);
	res->glue = ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_STAT) &
		    GLUE_STAT_MASK;
	res->irq = max_t(unsigned int, atomic_read(&omo_irq_count),
			 atomic_read(&omo_irq2_count));
}

static int omo_write_result(const char *dir, const struct omo_hyp_result *r)
{
	char path[256], buf[512];
	int n;

	n = scnprintf(buf, sizeof(buf),
		"label=%s\nparams=%s\nout0_cleared=%c\nsr1c=0x%08x\nglue=0x%08x\nirq=%u\n",
		r->label, r->params[0] ? r->params : "-", r->out0,
		r->sr1c, r->glue, r->irq);
	if (dir && dir[0]) {
		snprintf(path, sizeof(path), "%s/result.txt", dir);
		return omo_write_file(path, buf, n);
	}
	return 0;
}

static void omo_parse_entry_params(const char *params, char *hyp, size_t hyplen,
				   unsigned int *arg)
{
	char tmp[OMOH_PARAM_MAX];
	char *p, *tok;

	snprintf(tmp, sizeof(tmp), "%s", params);
	p = tmp;
	while ((tok = strsep(&p, ",")) != NULL) {
		while (*tok == ' ' || *tok == '\t')
			tok++;
		if (!strncmp(tok, "hyp=", 4))
			snprintf(hyp, hyplen, "%s", tok + 4);
		else if (!strncmp(tok, "arg=", 4))
			kstrtouint(tok + 4, 0, arg);
	}
}

/* --- single-hypothesis mode (BOOT B / controls) ------------------------ */

static void omo_single_run(void)
{
	struct omo_hyp_result res;
	char hyp[32];
	char buf[256];
	unsigned int arg = omo_arg;
	int blen;

	snprintf(hyp, sizeof(hyp), "%s",
		 (omo_hyp && omo_hyp[0]) ? omo_hyp : "none");
	memset(&res, 0, sizeof(res));
	snprintf(res.label, sizeof(res.label), "single");
	snprintf(res.params, sizeof(res.params), "hyp=%s,arg=%u", hyp, arg);

	pr_info("omo-hccaccept: single hypothesis hyp=%s arg=%u window=%ums\n",
		hyp, arg, omo_hccwin);
	omo_hyp_base_reset();
	if (!strcmp(hyp, "out5") && omo_resultpath && omo_resultpath[0]) {
		blen = scnprintf(buf, sizeof(buf),
			"stage=pre-out5 hyp=out5 arg=%u sr1c=0x%08x out0=0x%08x out1=0x%08x glue=0x%08x irq=%u\n",
			arg, omo_sr_rptr(0),
			ioread32(omo_bar0 + omo_mbox[0].off),
			ioread32(omo_bar0 + omo_mbox[1].off),
			ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_STAT) & GLUE_STAT_MASK,
			max_t(unsigned int, atomic_read(&omo_irq_count),
			      atomic_read(&omo_irq2_count)));
		omo_write_file(omo_resultpath, buf, blen);
	}
	omo_hyp_apply(hyp, arg);
	omo_sample_entry(&res, omo_hccwin);
	pr_info("omo-hccaccept: single result hyp=%s out0_cleared=%c sr1c=0x%08x glue=0x%08x irq=%u id1=%d fetched=%d\n",
		hyp, res.out0, res.sr1c, res.glue, res.irq, res.id1,
		res.fetched);
	if (res.out0 == 'y')
		pr_info("omo-hccaccept: *** out[0] CLEARED BY DEVICE - H2D ACCEPT BREAKTHROUGH (hyp=%s) ***\n",
			hyp);
	if (omo_resultpath && omo_resultpath[0]) {
		blen = scnprintf(buf, sizeof(buf),
			"stage=post hyp=%s arg=%u out0_cleared=%c sr1c=0x%08x glue=0x%08x irq=%u id1=%d fetched=%d\n",
			hyp, arg, res.out0, res.sr1c, res.glue, res.irq,
			res.id1, res.fetched);
		omo_write_file(omo_resultpath, buf, blen);
	}
}

/* --- batch mode (BOOT A) ----------------------------------------------- */

static int omo_batch_run(void)
{
	char *list = NULL, *cursor, *line;
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	unsigned int nn = 0;
	int stopped = 0, rc = 0;
	char sum[2048];
	size_t sumlen = 0;
	char path[128];

	rc = omo_mkdir(omo_batchdir);
	if (rc)
		pr_warn("omo-hccaccept: cannot create batchdir %s rc=%d\n",
			omo_batchdir, rc);

	f = filp_open(omo_batch, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-hccaccept: batch list %s open failed %ld\n",
		       omo_batch, PTR_ERR(f));
		return PTR_ERR(f);
	}
	list = vmalloc(16384);
	if (!list) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	n = kernel_read(f, list, 16383, &pos);
	filp_close(f, NULL);
	if (n <= 0) {
		vfree(list);
		return -EIO;
	}
	list[n] = 0;
	pr_info("omo-hccaccept: batch list %s (%zd bytes), dir %s, window %ums, stopfirst=%u\n",
		omo_batch, n, omo_batchdir, omo_hccwin, omo_stopfirst);

	cursor = list;
	while ((line = strsep(&cursor, "\n")) != NULL) {
		char label[OMOH_LABEL_MAX];
		char params[OMOH_PARAM_MAX];
		char hyp[32] = "none";
		char dir[256];
		char *bar;
		unsigned int arg = omo_arg;
		struct omo_hyp_result res;
		size_t ll;

		ll = strlen(line);
		if (ll && line[ll - 1] == '\r')
			line[--ll] = 0;
		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line || *line == '#')
			continue;
		bar = strchr(line, '|');
		if (!bar) {
			pr_warn("omo-hccaccept: batch line without '|': %s\n",
				line);
			continue;
		}
		*bar = 0;
		snprintf(label, sizeof(label), "%s", line);
		snprintf(params, sizeof(params), "%s", bar + 1);
		nn++;

		omo_parse_entry_params(params, hyp, sizeof(hyp), &arg);
		memset(&res, 0, sizeof(res));
		snprintf(res.label, sizeof(res.label), "%s", label);
		snprintf(res.params, sizeof(res.params), "%s", params);
		snprintf(dir, sizeof(dir), "%s/%02u-%s", omo_batchdir, nn,
			 label);
		if (omo_mkdir(dir))
			pr_warn("omo-hccaccept: mkdir %s failed\n", dir);

		if (stopped) {
			res.out0 = '?';
			pr_info("omo-hccaccept: [%s] SKIPPED (series stopped)\n",
				label);
			omo_write_result(dir, &res);
			continue;
		}

		pr_info("omo-hccaccept: [entry %u %s] hyp=%s arg=%u starting\n",
			nn, label, hyp, arg);
		omo_hyp_base_reset();
		omo_hyp_apply(hyp, arg);
		omo_sample_entry(&res, omo_hccwin);
		pr_info("omo-hccaccept: [entry %u %s] out0_cleared=%c sr1c=0x%08x glue=0x%08x irq=%u id1=%d fetched=%d\n",
			nn, label, res.out0, res.sr1c, res.glue, res.irq,
			res.id1, res.fetched);
		omo_write_result(dir, &res);

		if (nn == 1 || !strcmp(hyp, "none"))
			omo_ctrl_fetched = res.fetched;
		if (sumlen < sizeof(sum) - 80)
			sumlen += scnprintf(sum + sumlen, sizeof(sum) - sumlen,
				"%s|%c|0x%08x|0x%08x|%u\n", label,
				res.out0, res.sr1c, res.glue, res.irq);

		if (omo_stopfirst &&
		    (res.out0 == 'y' || res.id1 ||
		     (!omo_ctrl_fetched && res.fetched))) {
			stopped = 1;
			pr_info("omo-hccaccept: series STOPPED at entry %u %s (out0_cleared=%c id1=%d fetched=%d)\n",
				nn, label, res.out0, res.id1, res.fetched);
		}
	}

	snprintf(path, sizeof(path), "%s/batch-summary.txt", omo_batchdir);
	omo_write_file(path, sum, sumlen);
	snprintf(path, sizeof(path), "%s/batch.done", omo_batchdir);
	omo_write_file(path, "done\n", 5);
	pr_info("omo-hccaccept: done (batch entries=%u stopped=%d)\n", nn, stopped);
	vfree(list);
	return 0;
}

/* ---- init / exit -------------------------------------------------------- */

static void omo_free_rings(void)
{
	unsigned int i;

	for (i = 0; i < ETE_SR_N; i++) {
		if (omo_sr_va[i]) {
			dma_free_coherent(&omo_dev->dev, (ETE_DEPTH + 2) * 8,
					  omo_sr_va[i], omo_sr_dma[i]);
			omo_sr_va[i] = NULL;
		}
		if (omo_sr_pay[i]) {
			dma_free_coherent(&omo_dev->dev,
					  ETE_DEPTH * ETE_SR_PAYLOAD,
					  omo_sr_pay[i], omo_sr_pay_dma[i]);
			omo_sr_pay[i] = NULL;
		}
		kfree(omo_sr_snap[i]);
		kfree(omo_sr_pay_snap[i]);
		omo_sr_snap[i] = NULL;
		omo_sr_pay_snap[i] = NULL;
	}
	for (i = 0; i < ETE_DR_N; i++) {
		if (omo_dr_va[i]) {
			dma_free_coherent(&omo_dev->dev, ETE_DEPTH * 8,
					  omo_dr_va[i], omo_dr_dma[i]);
			omo_dr_va[i] = NULL;
		}
		if (omo_dr_pay[i]) {
			dma_free_coherent(&omo_dev->dev, ETE_DR_PAYLOAD,
					  omo_dr_pay[i], omo_dr_pay_dma[i]);
			omo_dr_pay[i] = NULL;
		}
		kfree(omo_dr_snap[i]);
		kfree(omo_pay_snap[i]);
		omo_dr_snap[i] = NULL;
		omo_pay_snap[i] = NULL;
	}
	omo_ete_ready = 0;
}

/*
 * Read the same device registers through BOTH endpoints' region-3 viewports
 * and log them side by side.  The on-chip space is aliased (the phase-21
 * sibling report proved reads are byte-identical), so this is the per-endpoint
 * evidence the task asks for: SR+0x18/+0x1c, the device index, out[0]/out[1]
 * and the glue status as seen through ep1 and ep0.
 */
static void omo_dump_dual(const char *tag)
{
	unsigned int i;
	void __iomem *wa = omo_bar0 + ETE_BAR0_OFF;
	void __iomem *wb = omo_bar0b ? omo_bar0b + ETE_BAR0_OFF : NULL;

	pr_info("omo-hccaccept: [dual %s +%lums] primary(ep1) BAR0=0x%llx secondary(ep0) BAR0=0x%llx\n",
		tag, omo_ms_now(), (unsigned long long)omo_bar0_base,
		(unsigned long long)omo_bar0b_base);
	for (i = 0; i < ETE_SR_N; i++) {
		u32 pw = ioread32(wa + omo_sr_block[i] + ETE_SR_WPTR);
		u32 pr = ioread32(wa + omo_sr_block[i] + ETE_SR_RPTR);
		u32 sw = wb ? ioread32(wb + omo_sr_block[i] + ETE_SR_WPTR) : 0;
		u32 sr = wb ? ioread32(wb + omo_sr_block[i] + ETE_SR_RPTR) : 0;

		pr_info("omo-hccaccept: [dual %s] SR ch%u wptr(dev idx +0x18)/rptr(+0x1c): ep1=0x%08x/0x%08x ep0=0x%08x/0x%08x\n",
			tag, i, pw, pr, sw, sr);
	}
	pr_info("omo-hccaccept: [dual %s] out[0]=0x%08x out[1]=0x%08x glue+0x2ec=0x%08x (ep1) | ep0 out[0]=0x%08x out[1]=0x%08x glue=0x%08x\n",
		tag, ioread32(omo_bar0 + omo_mbox[0].off),
		ioread32(omo_bar0 + omo_mbox[1].off),
		wb ? ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_STAT) : 0,
		wb ? ioread32(omo_bar0b + omo_mbox[0].off) : 0,
		wb ? ioread32(omo_bar0b + omo_mbox[1].off) : 0,
		wb ? ioread32(omo_bar0b + GLUE_BAR0_OFF + GLUE_STAT) : 0);
}

static int __init omo_svc_init(void)
{
	u16 cmd0 = 0, cmd1 = 0, rb = 0;
	unsigned int after_tgt, polls, k;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-hccaccept: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		pr_info("omo-hccaccept: endpoint %04x:00:00.0 id %04x:%04x\n",
			omo_domain, id & 0xffff, id >> 16);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-hccaccept: unexpected id\n");
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-hccaccept: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-hccaccept: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-hccaccept");
	if (ret) {
		pr_err("omo-hccaccept: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-hccaccept: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	{
		u32 lo = 0, hi = 0, b2 = 0;

		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_2, &b2);
		omo_bar0_base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				((u64)hi << 32);
		pr_info("omo-hccaccept: BAR0 base=0x%llx (config space), BAR2=0x%x (iatu_bar1)\n",
			(unsigned long long)omo_bar0_base,
			(u32)(b2 & PCI_BASE_ADDRESS_MEM_MASK));
	}

	omo_bar0 = pci_iomap(omo_dev, OMO_BAR_NUM, 0);
	omo_iatu = pci_iomap(omo_dev, OMO_IATU_BAR, 0);
	if (!omo_bar0 || !omo_iatu) {
		pr_err("omo-hccaccept: pci_iomap FAILED (bar0=%p iatu=%p)\n",
		       omo_bar0, omo_iatu);
		ret = -ENOMEM;
		goto err_release;
	}

	/* ---- second endpoint: claim + map (phase 21b) ---- */
	omo_dev2 = pci_get_domain_bus_and_slot(omo_domain2, 0, PCI_DEVFN(0, 0));
	if (!omo_dev2) {
		pr_err("omo-hccaccept: endpoint %04x:00:00.0 not found\n",
		       omo_domain2);
		ret = -ENODEV;
		goto err_unmap;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev2, 0x00, &id);
		pr_info("omo-hccaccept: second endpoint %04x:00:00.0 id %04x:%04x\n",
			omo_domain2, id & 0xffff, id >> 16);
	}
	{
		u16 c0 = 0, c1 = 0;

		pci_read_config_word(omo_dev2, PCI_COMMAND, &c0);
		ret = pci_enable_device(omo_dev2);
		if (ret) {
			pr_err("omo-hccaccept: ep0 pci_enable_device rc=%d\n", ret);
			goto err_unmap;
		}
		pci_read_config_word(omo_dev2, PCI_COMMAND, &c1);
		pr_info("omo-hccaccept: ep0 pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
			c0, c1);
	}
	ret = pci_request_mem_regions(omo_dev2, "omo-hccaccept-ep0");
	if (ret) {
		pr_err("omo-hccaccept: ep0 pci_request_mem_regions rc=%d\n", ret);
		goto err_unmap;
	}
	pr_info("omo-hccaccept: ep0 pci_request_mem_regions rc=0 (MEM BARs claimed)\n");
	{
		u32 lo = 0, hi = 0, b2 = 0;

		pci_read_config_dword(omo_dev2, PCI_BASE_ADDRESS_0, &lo);
		pci_read_config_dword(omo_dev2, PCI_BASE_ADDRESS_1, &hi);
		pci_read_config_dword(omo_dev2, PCI_BASE_ADDRESS_2, &b2);
		omo_bar0b_base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				 ((u64)hi << 32);
		pr_info("omo-hccaccept: ep0 BAR0 base=0x%llx, BAR2=0x%x (iatu)\n",
			(unsigned long long)omo_bar0b_base,
			(u32)(b2 & PCI_BASE_ADDRESS_MEM_MASK));
	}
	omo_bar0b = pci_iomap(omo_dev2, OMO_BAR_NUM, 0);
	omo_iatub = pci_iomap(omo_dev2, OMO_IATU_BAR, 0);
	if (!omo_bar0b || !omo_iatub) {
		pr_err("omo-hccaccept: ep0 pci_iomap FAILED (bar0=%p iatu=%p)\n",
		       omo_bar0b, omo_iatub);
		ret = -ENOMEM;
		goto err_unmap;
	}

	if (omo_scanlen) {
		omo_scan_base = vmalloc(omo_scanlen);
		if (!omo_scan_base) {
			ret = -ENOMEM;
			goto err_unmap;
		}
	}

	omo_win.devva_base = omo_devva_base;
	omo_win.devva_end = omo_devva_end;
	omo_win.hostca_base = omo_hostca_base;
	pr_info("omo-hccaccept: hostca->devva window: win[0]=0x%08x win[8]=0x%08x win[0x10]=0x%08x (chip->[4]->[0xc4]; pcie_hostca_to_devva @0xaefc)\n",
		omo_win.devva_base, omo_win.devva_end, omo_win.hostca_base);

	if (!omo_program) {
		pr_info("omo-hccaccept: program=0: iATU NOT programmed (read-only run)\n");
		goto skip_program;
	}
	if (ioread32(omo_iatu + OMO_IATU_CTRL2) == 0xffffffffU) {
		pr_err("omo-hccaccept: iatu window reads 0xffffffff - undecoded, refusing\n");
		ret = -EIO;
		goto err_unmap;
	}
	pr_info("omo-hccaccept: programming six inbound viewports on BOTH RCs via BAR2 (vendor membar path)\n");
	pr_info("omo-hccaccept: ep1 programmed %d viewports\n",
		omo_program_regions(omo_iatu, omo_bar0_base, "primary/ep1"));
	if (ioread32(omo_iatub + OMO_IATU_CTRL2) == 0xffffffffU) {
		pr_err("omo-hccaccept: ep0 iatu reads 0xffffffff - undecoded\n");
		ret = -EIO;
		goto err_unmap;
	}
	pr_info("omo-hccaccept: ep0 programmed %d viewports\n",
		omo_program_regions(omo_iatub, omo_bar0b_base, "secondary/ep0"));

	pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-hccaccept: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	/* The missing piece from rtmsg: the outbound (device->host) window. */
	pr_info("omo-hccaccept: outbound viewport BEFORE: [0x000]=0x%08x [0x004]=0x%08x [0x008]=0x%08x [0x010]=0x%08x [0x014]=0x%08x\n",
		ioread32(omo_iatu + 0x000), ioread32(omo_iatu + 0x004),
		ioread32(omo_iatu + 0x008), ioread32(omo_iatu + 0x010),
		ioread32(omo_iatu + 0x014));
	pr_info("omo-hccaccept: ep0 outbound BEFORE: [0x004]=0x%08x [0x008]=0x%08x [0x010]=0x%08x [0x014]=0x%08x\n",
		ioread32(omo_iatub + 0x004), ioread32(omo_iatub + 0x008),
		ioread32(omo_iatub + 0x010), ioread32(omo_iatub + 0x014));
	if (omo_outwin) {
		omo_program_outbound(omo_iatu, "primary/ep1");
		omo_program_outbound(omo_iatub, "secondary/ep0");
	}
	pr_info("omo-hccaccept: outbound viewport AFTER:  [0x000]=0x%08x [0x004]=0x%08x [0x008]=0x%08x [0x010]=0x%08x [0x014]=0x%08x\n",
		ioread32(omo_iatu + 0x000), ioread32(omo_iatu + 0x004),
		ioread32(omo_iatu + 0x008), ioread32(omo_iatu + 0x010),
		ioread32(omo_iatu + 0x014));
	pr_info("omo-hccaccept: ep0 outbound AFTER:  [0x000]=0x%08x [0x004]=0x%08x [0x008]=0x%08x [0x010]=0x%08x [0x014]=0x%08x\n",
		ioread32(omo_iatub + 0x000), ioread32(omo_iatub + 0x004),
		ioread32(omo_iatub + 0x008), ioread32(omo_iatub + 0x010),
		ioread32(omo_iatub + 0x014));

	after_tgt = ioread32(omo_bar0 + omo_target);
	pr_info("omo-hccaccept: decode verdict BAR0+0x%lx: after=0x%08x decoded=%s\n",
		omo_target, after_tgt,
		after_tgt != 0xffffffffU ? "YES" : "NO");

skip_program:
	/* Build the runtime message context (host structures only). */
	if (omo_msgctx_build()) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	/* Allocate the ETE ring node arrays and payload buffers, then program
	 * the SR/DR registers exactly as pcie_ete_init does. */
	if (omo_rings && omo_rings_alloc()) {
		ret = -ENOMEM;
		goto err_ctx;
	}
	if (omo_rings)
		omo_ete_program();

	/* Post device->host receive buffers (shuangta_ete_dr_dscr_fill
	 * @0x1765c: node.word0 = buffer device address) so the device has a
	 * target if it sends a payload.  Host memory only. */
	omo_post_dr();
	omo_scan_dr("postdr");	/* pre-release baseline for the device index */

	/* Post the host->device SR nodes and commit the SR producer index
	 * (shuangta_ete_sr_dscr_fill @0x17858 + sr_reg_init @0x14a48) so the
	 * SR path has a descriptor and a message in it.  Host memory only. */
	omo_post_sr();
	omo_snapshot_all();	/* SR post baseline (our own fill, host memory) */

	ret = omo_load_fw();
	if (ret)
		goto err_ctx;
	if (omo_write_fw() != 0) {
		pr_err("omo-hccaccept: firmware write refused/failed - stopping\n");
		goto err_ctx;
	}

	omo_t0 = jiffies;
	omo_snapshot_all();
	pr_info("omo-hccaccept: pre-release baseline taken (t0)\n");
	omo_poll_mailbox("pre", 1);
	omo_dump_gate_regs("pre");

	if (omo_useirq)
		omo_request_irq_line();
	if (omo_useirq2)
		omo_request_irq2();

	if (!omo_release) {
		pr_info("omo-hccaccept: release=0: NOT releasing the chip\n");
		goto done;
	}
	if (omo_do_release()) {
		pr_err("omo-hccaccept: release readback mismatch - stopping\n");
		goto err_ctx;
	}

	omo_poll_mailbox("post0", 0);
	omo_msg_service("post0");
	omo_scan_changes("post0");
	omo_scan_dr("post0");
	omo_scan_sr("post0");
	/* post-release ring/instance correction: the device rewrote the SR block
	 * when the firmware booted; re-assert the quoted program and re-sync
	 * the host producer to the device consumer before pumping. */
	/* Part A trigger: the firmware's pcie_msg_init runs after our pre-release
	 * ETE-interrupt write; re-assert the quoted vendor target now that the
	 * firmware is up, then dump the field-by-field state. */
	omo_ete_intr_reassert("post0");
	omo_dump_full("post0");
	omo_dump_dual("post0");
	omo_ete_resync_sr("post0");
	omo_seq_program("post0");
	/* the device-side gate this phase recovers (Part A); quoted writes only */
	omo_set_enable(omo_enable);
	omo_dump_gate_regs("gate");
	/* ring the host->device doorbell (pcie_msg_send(chip,3), @0x160f4) */
	omo_send_doorbell("post0", MSG_SEND_ID);
	omo_record_base();
	if (omo_batch && omo_batch[0]) {
		omo_batch_run();
		goto done;
	}
	omo_single_run();

	if (!omo_svc) {
		/* svc=0: the phase-20f synchronous poll loop, kept as control */
		if (omo_polldur < 10000)
			omo_polldur = 10000;
		if (omo_pollms < 20)
			omo_pollms = 20;
		polls = omo_polldur / omo_pollms;
		for (k = 0; k < polls; k++) {
			msleep(omo_pollms);
			omo_poll_mailbox("poll", 0);
			omo_msg_service("poll");
			omo_scan_dr("poll");
			omo_scan_sr("poll");
			if (k == 0)
				omo_send_doorbell("poll", MSG_SEND_ID);
		}
		omo_msg_service("final");
		omo_scan_dr("final");
		omo_scan_sr("final");
		goto done;
	}
	if (omo_svcdur < 20000)
		omo_svcdur = 20000;
	if (omo_svcms < 20)
		omo_svcms = 20;
	if (omo_svcdoorbell < 1)
		omo_svcdoorbell = 1;
	omo_out0_last = ioread32(omo_bar0 + omo_mbox[0].off);
	omo_out1_last = ioread32(omo_bar0 + omo_mbox[1].off);
	omo_glue_last = ioread32(omo_bar0 + GLUE_BAR0_OFF + GLUE_STAT);
	omo_svc_task = kthread_run(omo_svc_thread, NULL, "omo-hccaccept");
	if (IS_ERR(omo_svc_task)) {
		pr_err("omo-hccaccept: kthread_run failed rc=%ld\n", PTR_ERR(omo_svc_task));
		omo_svc_task = NULL;
		goto done;
	}
	wait_for_completion(&omo_svc_done);
	omo_svc_task = NULL;

done:
	pr_info("omo-hccaccept: done (release=%u rings=%u sr_posted=%u acpoff=%d svc=%u iters=%u glue_clears=%u pollms=%u polldur=%u irq=%d irq_taken=%d irq_handled=%d msgs=%u services=%u sendflag=%u dr_events=%u sr_events=%u pumped=%u)\n",
		omo_release, omo_rings, omo_sr_posted, omo_acpoff, omo_svc,
		omo_svc_iters, omo_glue_clears, omo_pollms,
		omo_polldur, omo_irq_num, atomic_read(&omo_irq_count),
		atomic_read(&omo_irq_hits), omo_msgs, omo_svc_count,
		omo_send_flag, omo_dr_events, omo_sr_events, omo_pumped);
	pr_info("omo-hccaccept: done ep0 (irq=%d irq_taken=%d irq_handled=%d) iatu_program=%u outwin=%u seq=%u\n",
		omo_irq2_num, atomic_read(&omo_irq2_count),
		atomic_read(&omo_irq2_hits), omo_program, omo_outwin, omo_seq);
	omo_release_irq();
	return 0;

err_ctx:
	omo_release_irq();
	omo_free_rings();
	kfree(omo_ctx.table);
	omo_ctx.table = NULL;
	if (omo_fw)
		vfree(omo_fw);
err_unmap:
	if (omo_scan_base)
		vfree(omo_scan_base);
	if (omo_iatub)
		pci_iounmap(omo_dev2, omo_iatub);
	if (omo_bar0b)
		pci_iounmap(omo_dev2, omo_bar0b);
	if (omo_iatu)
		pci_iounmap(omo_dev, omo_iatu);
	if (omo_bar0)
		pci_iounmap(omo_dev, omo_bar0);
	if (omo_dev2) {
		pci_release_mem_regions(omo_dev2);
		pci_disable_device(omo_dev2);
		pci_dev_put(omo_dev2);
		omo_dev2 = NULL;
	}
err_release:
	if (omo_dev)
		pci_release_mem_regions(omo_dev);
err_disable:
	if (omo_dev)
		pci_disable_device(omo_dev);
err_put:
	if (omo_dev)
		pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_svc_exit(void)
{
	if (omo_svc_task) {
		kthread_stop(omo_svc_task);
		omo_svc_task = NULL;
	}
	omo_release_irq();
	kfree(omo_ctx.table);
	omo_ctx.table = NULL;
	omo_free_rings();
	if (omo_fw)
		vfree(omo_fw);
	if (omo_scan_base)
		vfree(omo_scan_base);
	if (omo_iatub)
		pci_iounmap(omo_dev2, omo_iatub);
	if (omo_bar0b)
		pci_iounmap(omo_dev2, omo_bar0b);
	if (omo_iatu)
		pci_iounmap(omo_dev, omo_iatu);
	if (omo_bar0)
		pci_iounmap(omo_dev, omo_bar0);
	if (omo_dev2) {
		pci_release_mem_regions(omo_dev2);
		pci_disable_device(omo_dev2);
		pci_dev_put(omo_dev2);
		omo_dev2 = NULL;
	}
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
	}
	pr_info("omo-hccaccept: unloaded\n");
}

module_init(omo_svc_init);
module_exit(omo_svc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("phase-22 hccaccept: bothep + exp-harness batch (H2/H3/H4 + out[5]) for the device-side H2D HCC accept gate");
