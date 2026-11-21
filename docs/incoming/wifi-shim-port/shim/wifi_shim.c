// SPDX-License-Identifier: GPL-2.0
/*
 * wifi_shim.c - DRAFT compatibility shim for loading the vendor Hi5622V100
 * 5.10.201 Wi-Fi binaries (hi5622v100_plat.ko / hi5622v100_wifi.ko) on our
 * 6.18.60 OpenWrt kernel (luofu / Hi5671YV200).
 *
 * SCOPE AND STATUS
 *   Host-produced DESIGN DRAFT (see REPORT.md / shim/README.md). Exports the
 *   5.10-named kernel symbols our 6.18 vmlinux no longer exports, mapped to
 *   6.18 equivalents. NOT sufficient to make the vendor modules work: M3 shows
 *   pervasive baked 5.10 struct offsets (net_device, sk_buff, genl_info,
 *   hrtimer) that no shim can redirect. See SUMMARY.md for the NO-GO verdict.
 *
 * Symbol table (M1, machine-generated, 16 shim symbols + stubs):
 *   5.10 name                 6.18 mapping
 *   printk                    vprintk()
 *   __kmalloc                 __kmalloc_noprof()
 *   kmalloc_order_trace       alloc_pages_exact_noprof()  [trace lost]
 *   kmem_cache_alloc_trace    kmem_cache_alloc_noprof()   [trace lost]
 *   vmalloc                   vmalloc_noprof()
 *   alloc_workqueue           alloc_workqueue_noprof()    [fmt pre-expanded]
 *   kfree_skb                 __kfree_skb()
 *   consume_skb               __kfree_skb()               [no drop accounting]
 *   __dev_kfree_skb_any       dev_kfree_skb_any_reason()
 *   skb_queue_purge           skb_queue_purge_reason()
 *   hrtimer_init              hrtimer_setup() + stub cb   [6.13 rework, 4 args]
 *   kern_path_create          kern_path() + lookup_one()  [reconstructed]
 *   done_path_create          path_put() + dput()         [vendor ns]
 *   dma_sync_single_for_cpu   dev->dma_ops direct call    [inline body]
 *   dma_sync_single_for_device  ditto
 *   mem_section               FLATMEM stand-in array      [see notes]
 *   wireless_send_event       no-op (WEXT events dropped)
 *   PDE_DATA                  pde_data() (for hi_pcie.ko)
 *
 * Vendor hi_* platform symbols are NOT stubbed here: satisfied by the other
 * vendor .ko binaries (closure depth<=3, M1):
 *   hi5622v100_wifi.ko <- hi5622v100_plat.ko (138 symbols: oal_, oam_, hcc_ ...)
 *   hi5622v100_wifi.ko <- cfg80211.ko       (26 symbols: wiphy_, cfg80211_ ...)
 *   both              <- ksecurec.ko        (11: memcpy_s, memset_s, sprintf_s, ...)
 *   both              <- hi_kpie.ko         (2)
 *   hi5622v100_plat.ko <- ksecurec.ko (8), hi_kpie.ko (1)
 * Only done_path_create (vendor ns, no provider found) gets a stub here.
 */
#include <linux/kconfig.h>

/* 6.18 turned several 5.10 exported functions into macros/static inlines.
 * Rename the inlines via #define so we can re-export REAL functions under the
 * 5.10 names, then #undef the renames. */
#define printk                    printk_inline_6x
#define vmalloc                   vmalloc_inline_6x
#define alloc_workqueue           alloc_workqueue_inline_6x
#define kfree_skb                 kfree_skb_inline_6x
#define consume_skb               consume_skb_inline_6x
#define skb_queue_purge           skb_queue_purge_inline_6x
#define dma_sync_single_for_cpu   dma_sync_single_for_cpu_inline_6x
#define dma_sync_single_for_device dma_sync_single_for_device_inline_6x

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/dma-map-ops.h>
#include <net/dropreason-core.h>
#include <linux/hrtimer.h>
#include <linux/dma-mapping.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#undef printk
#undef vmalloc
#undef alloc_workqueue
#undef kfree_skb
#undef consume_skb
#undef skb_queue_purge
#undef dma_sync_single_for_cpu
#undef dma_sync_single_for_device

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wifi-shim-port lane (host-only draft)");
MODULE_DESCRIPTION("5.10->6.18 symbol shim for vendor hi5622v100 wifi binaries");

extern void dev_kfree_skb_any_reason(struct sk_buff *skb, enum skb_drop_reason reason);

/* ---- printk (5.10 export) -> vprintk (6.18 export) ---- */
int printk(const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vprintk(fmt, ap);
	va_end(ap);
	return r;
}
EXPORT_SYMBOL(printk);

/* ---- allocator _noprof renames (6.18 CONFIG_MEM_ALLOC_PROFILING) ---- */
void *__kmalloc(size_t size, gfp_t flags)
{
	return __kmalloc_noprof(size, flags);
}
EXPORT_SYMBOL(__kmalloc);

void *kmem_cache_alloc_trace(struct kmem_cache *cachep, gfp_t flags, size_t size)
{
	return kmem_cache_alloc_noprof(cachep, flags);
}
EXPORT_SYMBOL(kmem_cache_alloc_trace);

void *kmalloc_order_trace(size_t size, gfp_t flags, unsigned int order)
{
	return alloc_pages_exact_noprof(size, flags);
}
EXPORT_SYMBOL(kmalloc_order_trace);

void *vmalloc(unsigned long size)
{
	return vmalloc_noprof(size);
}
EXPORT_SYMBOL(vmalloc);

struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags,
					 int max_active, ...)
{
	char buf[256];
	va_list ap;

	/* 6.18 exports only alloc_workqueue_noprof(fmt, flags, max_active, ...);
	 * __alloc_workqueue is not exported, so pre-expand the format string and
	 * call with no varargs. */
	va_start(ap, max_active);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return alloc_workqueue_noprof(buf, flags, max_active);
}
EXPORT_SYMBOL(alloc_workqueue);

/* ---- skb freeing APIs (kfree_skb/consume_skb are 6.18 static inlines) ---- */
void kfree_skb(struct sk_buff *skb)
{
	__kfree_skb(skb);
}
EXPORT_SYMBOL(kfree_skb);

void consume_skb(struct sk_buff *skb)
{
	/* No drop accounting; semantic note in README. */
	__kfree_skb(skb);
}
EXPORT_SYMBOL(consume_skb);

void __dev_kfree_skb_any(struct sk_buff *skb)
{
	dev_kfree_skb_any_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
}
EXPORT_SYMBOL(__dev_kfree_skb_any);

void skb_queue_purge(struct sk_buff_head *list)
{
	skb_queue_purge_reason(list, SKB_DROP_REASON_NOT_SPECIFIED);
}
EXPORT_SYMBOL(skb_queue_purge);

/* ---- hrtimer_init -> hrtimer_setup (6.13 rework: callback is now mandatory
 * and the driver's baked write of timer->function lands at the 5.10 offset of
 * the 6.18 hrtimer - a struct-layout drift in its own right, see M3) ---- */
static enum hrtimer_restart wifi_shim_hrtimer_stub(struct hrtimer *timer)
{
	return HRTIMER_NORESTART;
}

void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
		  enum hrtimer_mode mode)
{
	hrtimer_setup(timer, wifi_shim_hrtimer_stub, clock_id, mode);
}
EXPORT_SYMBOL(hrtimer_init);

/* ---- path-creation pair used by plat.ko for runtime file/dir setup ---- */
int kern_path_create(const char *name, unsigned int flags,
		     struct path *path, struct dentry **dentry)
{
	char *dir_buf, *base, *sep;
	struct dentry *d;
	int len, err;

	dir_buf = kstrdup(name, GFP_KERNEL);
	if (!dir_buf)
		return -ENOMEM;
	sep = strrchr(dir_buf, '/');
	if (!sep) {
		kfree(dir_buf);
		return -EINVAL;
	}
	base = sep + 1;
	if (sep != dir_buf)
		*sep = 0;
	else
		dir_buf[1] = 0;		/* "/base" -> dir "/" */
	err = kern_path(dir_buf, LOOKUP_DIRECTORY | flags, path);
	if (err) {
		kfree(dir_buf);
		return err;
	}
	len = strlen(base);
	{
		struct qstr q = QSTR_INIT(base, len);

		d = lookup_one(&nop_mnt_idmap, &q, path->dentry);
	}
	kfree(dir_buf);
	if (IS_ERR(d)) {
		path_put(path);
		return PTR_ERR(d);
	}
	*dentry = d;
	return 0;
}
EXPORT_SYMBOL(kern_path_create);

void done_path_create(struct path *parent, struct dentry *dentry)
{
	path_put(parent);
	dput(dentry);
}
EXPORT_SYMBOL(done_path_create);

/* ---- dma_sync_single_* (6.18 static inlines; replicate bodies via dev->dma_ops) ---- */
/* 6.18 body mirrored exactly (include/linux/dma-mapping.h) */
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
			     size_t size, enum dma_data_direction dir)
{
	if (dma_dev_need_sync(dev))
		__dma_sync_single_for_cpu(dev, addr, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
				size_t size, enum dma_data_direction dir)
{
	if (dma_dev_need_sync(dev))
		__dma_sync_single_for_device(dev, addr, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_device);

/* ---- mem_section (plat.ko oal_dma_map_single pfn-validity walk) ----
 * Vendor 5.10 kernel was CONFIG_SPARSEMEM; our 6.18 is CONFIG_FLATMEM and has
 * no mem_section at all. Stand-in: a vendor-5.10-LAYOUT struct for the board's
 * 128 MB DDR as ONE section, marked present so section-present tests pass.
 * WARNING: bit semantics are vendor-5.10 and NOT runtime-verified; this is the
 * single most dangerous stub here (see analysis/member-audit-summary.md). */
#define V510_SECTION_HAS_MEM_MAP  0x00000002UL
struct v510_mem_section {
	unsigned long section_mem_map;
	unsigned long *pageblock_flags;
};
struct v510_mem_section mem_section[1] = {
	{ .section_mem_map = V510_SECTION_HAS_MEM_MAP | 0x00000001UL,
	  .pageblock_flags = NULL },
};
EXPORT_SYMBOL(mem_section);

/* ---- WEXT event notifier (wext core absent on 6.18; dropping events) ---- */
struct net_device;
union iwreq_data;
int wireless_send_event(struct net_device *dev, unsigned int cmd,
			union iwreq_data *wrqu, const char *extra)
{
	return 0;
}
EXPORT_SYMBOL(wireless_send_event);

/* ---- hi_pcie.ko auxiliary stubs ---- */
void *PDE_DATA(const struct inode *inode)
{
	return pde_data(inode);
}
EXPORT_SYMBOL(PDE_DATA);

int of_get_named_gpio_flags(struct device_node *np, const char *propname,
			    int index, unsigned long *flags)
{
	if (flags)
		*flags = 0;
	return -ENOSYS;		/* draft: see README bench-notes */
}
EXPORT_SYMBOL(of_get_named_gpio_flags);

static int __init wifi_shim_init(void)
{
	pr_info("wifi_shim: 5.10->6.18 symbol shim loaded (draft, NO-GO verdict - see REPORT)\n");
	return 0;
}

static void __exit wifi_shim_exit(void)
{
}

module_init(wifi_shim_init);
module_exit(wifi_shim_exit);
