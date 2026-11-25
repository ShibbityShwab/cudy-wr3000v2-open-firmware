/* omo_h2dsampler v2 (mmap) - read-only host->device (H2D) trace sampler for the
 * Cudy WR3000 v2.0 stock (pair-A) image. Lane rx-h2d-window / task-194; v1 is
 * docs/incoming/wifi-rx-h2d-trace/tools/omo_h2dsampler.c (task-176).
 *
 * WHY v2 EXISTS (see docs/incoming/wifi-rx-h2d-window/REPORT.md §2):
 *   v1 read the ETE registers with pread() on /dev/mem. On this 32-bit ARM kernel
 *   read()/pread() of /dev/mem goes through read_mem() -> valid_phys_addr_range()
 *   (arch/arm/mm/mmap.c), which rejects every address below PHYS_OFFSET (RAM start,
 *   0x80600000 on the stock) with -EFAULT. Both ETE windows (0x403f2000 EP0,
 *   0x583f2000 EP1) are below it, so v1 could NEVER read a register: the returned
 *   error was ignored, base/depth stayed 0, and the run ended with "ring not seen".
 *   v2 mmaps the ETE window page (/dev/mem mmap is allowed and ioremaps - the same
 *   path busybox devmem uses) and keeps pread() only for host DRAM (ring nodes and
 *   message buffers, addresses >= PHYS_OFFSET, which is correct).
 *
 * WHAT IT DOES (READ-ONLY):
 *   Prints the HCC frames the vendor posts into the ETE SR ring as 8-byte nodes.
 *   It only: mmap()s a /dev/mem page READ-ONLY, pread()s /dev/mem (DRAM), writes its
 *   own log file, and stat()s /proc/modules + a stop file. No device writes.
 *
 * DEFAULT WINDOW: 0x583f2000 = EP1 (0001:00:00.0 = vendor chip->dev[0], phy_devid 0)
 *   region-3 host base 0x583b8000 + (ETE CA 0x4003a000 - CA base 0x40000000).
 *   0x403f2000 is EP0 (our port / PD4 image) and aliases the SAME on-chip register
 *   block (phase21/sibling-ep.md A.4), so -w 0x403f2000 works too; v1 used it.
 *
 * USAGE (on the stock root shell; run from the one-shot init aid or by hand):
 *   omo-h2dsampler -o /overlay/h2d-trace.log -t 240 -r 20 -w 0x583f2000 -W 180
 *   options:
 *     -o PATH   log file (append). default: /overlay/h2d-trace.log
 *     -t SEC    capture duration after the ring appears. default: 240
 *     -r MS     poll period. default: 20
 *     -w PHYS   ETE window host physical address. default: 0x583f2000 (EP1)
 *     -m NAME   module to wait for. default: hi5622v100_plat
 *     -s PATH   stop file; stop when it exists. default: /overlay/h2d-trace.stop
 *     -W SEC    max wait for the module/ring before giving up. default: 180
 *     -p        READ-ONLY PROBE mode: print the SR ch0..2 registers + one pread()
 *               control read, then exit (0 = ring looks valid, 1 = not). No log file.
 *     --selftest  host-side pure unit checks of the ring/address helpers.
 *     -q        quiet (only the log file)
 *
 * TEST HOOK: if env OMO_H2D_FAKE_WIN names a regular file, the window is mmap()ed
 *   from that file at offset 0 instead of /dev/mem (host tests only); /dev/mem is not
 *   opened, so DRAM preads fail and are counted as misreads.
 *
 * LOG FORMAT (version 2; the v1 format is kept, tools/decode_h2d.py still decodes it):
 *   # omo_h2dsampler v2 ts0=... window=... module=... duration=... period_ms=... access=mmap
 *   E ts_ms=... msg=win-probe base=... depth=... prod=... cons=... ctrl=... ring=yes|no
 *   R ts_ms=... ch=... ctrl=... base=... depth=... prod=... cons=...
 *   M ts_ms=... ch=... idx=... prime=... node=0x... len=... flags=0x... total=... group=...
 *     bid=0x... vap=... chip=... id=... dst=... hex=<full frame hex>
 *   E ts_ms=... msg=heartbeat misreads=... win_err=...
 *   E ts_ms=... msg=ring-seen dt_wait_ms=...
 *   E ts_ms=... msg=ring-not-seen wait_sec=... module_loaded=... last=base=... depth=...
 *   E ts_ms=... msg=stopped
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define ETE_DEFAULT_WINDOW  0x583f2000u   /* EP1: 0x583b8000 + (CA 0x4003a000 - 0x40000000) */
#define ETE_ALT_WINDOW      0x403f2000u   /* EP0 (our port): alias of the same register block */
#define WIN_MAP_SIZE        0x2000u
#define SR_BLOCK_OFF        0x400u        /* SR0 block inside the ETE window */
#define SR_STRIDE           0x50u
#define SR_CHANNELS         3
#define REG_CTRL            0x08u
#define REG_BASE            0x10u
#define REG_DEPTH           0x14u
#define REG_PROD            0x18u
#define REG_CONS            0x1cu
#define DRAM_LO             0x80000000u   /* stock DRAM window (boot log: 0x80600000-0x87ffffff) */
#define DRAM_HI             0x88000000u
#define DEPTH_MAX           1024u
#define NODE_MAX_LEN        0x700u
#define FRAME_MAX_LEN       NODE_MAX_LEN
#define HCC_TAG_OFF         10
#define HCC_TAG             0x5a5a

struct chan_state {
	uint32_t last_raw;
	int      have_last;
	uint32_t last_idx;
	int      last_phase;
};

static volatile sig_atomic_t g_stop;
static int mem_fd = -1;
static unsigned char *win_map;
static uint32_t win_page;
static uint32_t win_delta;      /* window - page-aligned map base */
static int win_is_fake;
static FILE *log_fp;
static int quiet;
static unsigned long long g_dram_misreads;
static unsigned long long g_win_misreads;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* Host DRAM reads (ring nodes, message buffers): pread() is correct here because
 * these addresses are >= PHYS_OFFSET, i.e. inside the kernel's direct map. */
static int phys_read(uint32_t phys, void *buf, size_t len)
{
	ssize_t n;
	if (mem_fd < 0) {
		g_dram_misreads++;
		return -1;
	}
	n = pread(mem_fd, buf, len, (off_t)phys);
	if (n != (ssize_t)len) {
		g_dram_misreads++;
		return -1;
	}
	return 0;
}

/* ETE register read: through the mmap()ed window page (v2; see the header comment). */
static int win_read32(uint32_t off_from_window, uint32_t *out)
{
	if (!win_map || win_delta + off_from_window + sizeof(*out) > WIN_MAP_SIZE) {
		g_win_misreads++;
		return -1;
	}
	memcpy(out, win_map + win_delta + off_from_window, sizeof(*out));
	return 0;
}

static int ring_valid(uint32_t base, uint32_t depth)
{
	if (base < DRAM_LO || base >= DRAM_HI)
		return 0;               /* 0, 0xffffffff, MMIO, ... are all "not programmed" */
	if (depth < 1 || depth > DEPTH_MAX)
		return 0;               /* depth is the depth-1 register on this device */
	return 1;
}

static int window_map_open(uint32_t window)
{
	const char *fake = getenv("OMO_H2D_FAKE_WIN");
	int fd;

	if (fake && *fake) {
		fd = open(fake, O_RDONLY);
		if (fd < 0)
			return -1;
		win_page = 0;
		win_delta = 0;
		win_is_fake = 1;
	} else {
		fd = mem_fd;            /* /dev/mem, O_RDONLY|O_SYNC, opened by the caller */
		if (fd < 0)
			return -1;
		win_page = window & ~0xFFFu;     /* mmap offset must be page-aligned */
		win_delta = window - win_page;
	}
	win_map = mmap(NULL, WIN_MAP_SIZE, PROT_READ, MAP_SHARED, fd, (off_t)win_page);
	if (win_map == MAP_FAILED) {
		win_map = NULL;
		if (win_is_fake)
			close(fd);
		return -1;
	}
	if (win_is_fake)
		close(fd);
	return 0;
}

static int module_loaded(const char *name)
{
	char line[512], want[128];
	FILE *f;
	int found = 0;
	snprintf(want, sizeof(want), "%s ", name);
	f = fopen("/proc/modules", "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, want, strlen(want))) {
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static int stop_file_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static void log_line(const char *fmt, ...)
{
	va_list ap;
	if (!log_fp)
		return;
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	va_end(ap);
	fputc('\n', log_fp);
	fflush(log_fp);
}

static void hex_dump(FILE *fp, const unsigned char *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		fprintf(fp, "%02x", b[i]);
}

static void emit_message(int ch, int prime, uint32_t idx, uint32_t node_addr,
			 uint32_t len, uint32_t flags, const unsigned char *frame,
			 uint64_t ts)
{
	uint16_t total = 0, id = 0;
	unsigned group, bid, vap, chip, dst;

	if (len < 12)
		return;
	total = (uint16_t)(frame[4] | (frame[5] << 8));
	id = (uint16_t)(frame[6] | (frame[7] << 8));
	group = frame[0] & 0x0f;
	bid = frame[1];
	vap = frame[2];
	chip = frame[3];
	dst = frame[9];
	fprintf(log_fp,
		"M ts_ms=%llu ch=%d idx=%u prime=%d node=0x%08x len=%u flags=0x%04x "
		"total=%u group=%u bid=0x%02x vap=%u chip=%u id=%u dst=%u hex=",
		(unsigned long long)ts, ch, idx, prime, node_addr, len, flags,
		total, group, bid, vap, chip, id, dst);
	hex_dump(log_fp, frame, len);
	fputc('\n', log_fp);
	fflush(log_fp);
}

/* Read the ring from last_idx/last_phase up to the current producer and emit every node. */
static void drain_channel(int ch, uint32_t base, uint32_t depth, uint32_t raw,
			  struct chan_state *st)
{
	uint32_t idx, phase, n, k;

	if (depth == 0 || depth > DEPTH_MAX)
		return;
	idx = raw & 0x3ffu;
	if (idx >= depth) {
		uint32_t tmp = idx % depth;
		if (tmp == 0 && idx != 0) {
			/* producer is a plain modulo counter: normalise */
			idx = 0;
		} else {
			idx = tmp;
		}
	}
	phase = (raw >> 10) & 1u;

	if (!st->have_last) {
		/* Prime: emit every currently valid node once. */
		for (k = 0; k < depth; k++) {
			uint32_t node_phys = base + k * 8u;
			uint32_t node[2];
			uint32_t len, flags;
			unsigned char frame[FRAME_MAX_LEN];
			if (phys_read(node_phys, node, sizeof(node)))
				continue;
			if (!node[0])
				continue;
			len = node[1] >> 16;
			flags = node[1] & 0x1fffu;
			if (len < 12 || len > FRAME_MAX_LEN)
				continue;
			if (phys_read(node[0], frame, len))
				continue;
			if ((uint16_t)(frame[HCC_TAG_OFF] | (frame[HCC_TAG_OFF + 1] << 8)) != HCC_TAG)
				continue;
			emit_message(ch, 1, k, node[0], len, flags, frame, now_ms());
		}
		st->have_last = 1;
		st->last_raw = raw;
		st->last_idx = idx;
		st->last_phase = phase;
		return;
	}

	if (raw == st->last_raw)
		return;

	n = (idx + depth - st->last_idx) % depth;
	if (n == 0) {
		/* More than depth messages since the last poll (or a same-index phase wrap):
		 * re-walk the whole ring; the decoder dedupes and quarantines torn reads. */
		n = depth;
	}

	for (k = 1; k <= n; k++) {
		uint32_t slot = (st->last_idx + k) % depth;
		uint32_t node_phys = base + slot * 8u;
		uint32_t node[2];
		uint32_t len, flags;
		unsigned char frame[FRAME_MAX_LEN];

		if (phys_read(node_phys, node, sizeof(node)))
			continue;
		if (!node[0])
			continue;
		len = node[1] >> 16;
		flags = node[1] & 0x1fffu;
		if (len < 12 || len > FRAME_MAX_LEN)
			continue;
		if (phys_read(node[0], frame, len))
			continue;
		if ((uint16_t)(frame[HCC_TAG_OFF] | (frame[HCC_TAG_OFF + 1] << 8)) != HCC_TAG)
			continue;
		emit_message(ch, 0, slot, node[0], len, flags, frame, now_ms());
	}

	st->last_raw = raw;
	st->last_idx = idx;
	st->last_phase = phase;
}

static void read_channel_regs(int ch, uint32_t *ctrl, uint32_t *base, uint32_t *depth,
			      uint32_t *prod, uint32_t *cons)
{
	uint32_t off = SR_BLOCK_OFF + (uint32_t)ch * SR_STRIDE;
	*ctrl = *base = *depth = *prod = *cons = 0;
	win_read32(off + REG_CTRL, ctrl);
	win_read32(off + REG_BASE, base);
	win_read32(off + REG_DEPTH, depth);
	win_read32(off + REG_PROD, prod);
	win_read32(off + REG_CONS, cons);
}

static int do_selftest(void)
{
	int pass = 0, total = 0;
	struct { uint32_t base, depth; int want; } rings[] = {
		{ 0x00000000u, 0x0000001fu, 0 },
		{ 0xfffffffeu, 0x00000000u, 0 },
		{ 0x848f6000u, 0x00000000u, 0 },
		{ 0x848f6000u, 0x00000401u, 0 },
		{ 0x848f6000u, 0x0000001fu, 1 },
		{ 0x83a52000u, 0x0000001fu, 1 },
		{ 0x80600000u, 0x00000001u, 1 },
		{ 0x87ffffffu, 0x00000400u, 1 },
		{ 0x87ffffffu, 0x00000401u, 0 },
	};
	size_t i;

	for (i = 0; i < sizeof(rings) / sizeof(rings[0]); i++) {
		total++;
		if (ring_valid(rings[i].base, rings[i].depth) == rings[i].want)
			pass++;
		else
			printf("SELFTEST FAIL ring base=0x%08x depth=0x%x want=%d\n",
			       rings[i].base, rings[i].depth, rings[i].want);
	}

	/* windows: BAR0 base + region-3 offset 0x3b8000 + (ETE CA 0x4003a000 - base 0x40000000) */
	{
		struct { uint32_t bar0, want; } wins[] = {
			{ 0x58000000u, 0x583f2000u },   /* EP1 */
			{ 0x40000000u, 0x403f2000u },   /* EP0 */
		};
		for (i = 0; i < sizeof(wins) / sizeof(wins[0]); i++) {
			total++;
			if (wins[i].bar0 + 0x3f2000u == wins[i].want)
				pass++;
			else
				printf("SELFTEST FAIL window bar0=0x%08x\n", wins[i].bar0);
		}
	}

	/* SR ch0 register offsets inside the window */
	{
		struct { uint32_t off, want; } regs[] = {
			{ SR_BLOCK_OFF + REG_CTRL, 0x408u },
			{ SR_BLOCK_OFF + REG_BASE, 0x410u },
			{ SR_BLOCK_OFF + REG_DEPTH, 0x414u },
			{ SR_BLOCK_OFF + REG_PROD, 0x418u },
			{ SR_BLOCK_OFF + REG_CONS, 0x41cu },
			{ SR_BLOCK_OFF + SR_STRIDE + REG_BASE, 0x460u },
		};
		for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
			total++;
			if (regs[i].off == regs[i].want)
				pass++;
			else
				printf("SELFTEST FAIL reg off=0x%x want=0x%x\n", regs[i].off, regs[i].want);
		}
	}

	printf("SELFTEST: %d/%d PASS\n", pass, total);
	return pass == total ? 0 : 1;
}

/* -p: read-only probe. Exit 0 if SR0 looks like a live ring, 1 otherwise. */
static int do_probe(uint32_t window)
{
	uint32_t base, depth, prod, cons, ctrl;
	int i;

	printf("omo-h2dsampler v2 probe window=0x%08x access=%s\n",
	       window, win_is_fake ? "file-fake" : "mmap");
	for (i = 0; i < SR_CHANNELS; i++) {
		read_channel_regs(i, &ctrl, &base, &depth, &prod, &cons);
		printf("SR ch%d ctrl=0x%08x base=0x%08x depth=0x%08x prod=0x%08x cons=0x%08x ring=%s\n",
		       i, ctrl, base, depth, prod, cons,
		       (i == 0 && ring_valid(base, depth)) ? "YES" : (depth ? "PARTIAL" : "NO"));
	}
	read_channel_regs(0, &ctrl, &base, &depth, &prod, &cons);

	/* The v1 control: pread() the SAME base register. Expected to fail with
	 * EFAULT (below PHYS_OFFSET) or, in fake mode, with EBADF/no /dev/mem. */
	{
		uint32_t v = 0;
		int rc = -1, saved = 0;
		if (mem_fd >= 0) {
			ssize_t n = pread(mem_fd, &v, sizeof(v), (off_t)(window + SR_BLOCK_OFF + REG_BASE));
			if (n == (ssize_t)sizeof(v))
				rc = 0;
			else
				saved = errno;
		} else {
			saved = EBADF;
		}
		if (rc == 0)
			printf("pread_control=OK value=0x%08x (kernel allows read() here; check REPORT §2)\n", v);
		else
			printf("pread_control=FAIL errno=%d (%s) -- expected: v1's read() cannot reach MMIO below RAM\n",
			       saved, strerror(saved));
	}
	printf("probe: ring=%s\n", ring_valid(base, depth) ? "YES" : "NO");
	fflush(stdout);
	return ring_valid(base, depth) ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *log_path = "/overlay/h2d-trace.log";
	const char *module = "hi5622v100_plat";
	const char *stop_path = "/overlay/h2d-trace.stop";
	uint32_t window = ETE_DEFAULT_WINDOW;
	unsigned duration = 240, period_ms = 20, wait_max = 180;
	uint64_t ts0, ts_ring = 0, last_state = 0;
	struct chan_state st[SR_CHANNELS];
	int opt, i, ring_seen = 0, probe_mode = 0;
	int probe_last_logged = 0;

	memset(st, 0, sizeof(st));
	/* --selftest is host-side only; handle it before getopt (no long options). */
	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--selftest"))
			return do_selftest();
	while ((opt = getopt(argc, argv, "o:t:r:w:m:s:W:pqh")) != -1) {
		switch (opt) {
		case 'o': log_path = optarg; break;
		case 't': duration = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'r': period_ms = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'w': window = (uint32_t)strtoul(optarg, NULL, 0); break;
		case 'm': module = optarg; break;
		case 's': stop_path = optarg; break;
		case 'W': wait_max = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'p': probe_mode = 1; break;
		case 'q': quiet = 1; break;
		default:
			fprintf(stderr,
				"usage: %s [-o log] [-t sec] [-r ms] [-w phys] [-m module] "
				"[-s stopfile] [-W waitsec] [-p] [--selftest] [-q]\n", argv[0]);
			return 2;
		}
	}
	(void)probe_last_logged;
	if (period_ms < 1)
		period_ms = 1;

	mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (mem_fd < 0) {
		const char *fake = getenv("OMO_H2D_FAKE_WIN");
		if (!(fake && *fake)) {
			fprintf(stderr, "omo-h2dsampler: /dev/mem open failed: %s\n", strerror(errno));
			return 3;
		}
	}
	if (window_map_open(window)) {
		fprintf(stderr, "omo-h2dsampler: window mmap failed (window=0x%08x): %s\n",
			window, strerror(errno));
		if (probe_mode)
			printf("window_mmap=FAIL window=0x%08x errno=%d (%s)\n",
			       window, errno, strerror(errno));
		if (mem_fd >= 0)
			close(mem_fd);
		return 5;
	}

	if (probe_mode) {
		int rc = do_probe(window);
		if (mem_fd >= 0)
			close(mem_fd);
		return rc;
	}

	log_fp = fopen(log_path, "a");
	if (!log_fp) {
		fprintf(stderr, "omo-h2dsampler: cannot open %s: %s\n", log_path, strerror(errno));
		if (mem_fd >= 0)
			close(mem_fd);
		munmap(win_map, WIN_MAP_SIZE);
		return 3;
	}
	setvbuf(log_fp, NULL, _IOLBF, 0);

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	ts0 = now_ms();
	log_line("# omo_h2dsampler v2 ts0=%llu window=0x%08x module=%s duration=%u period_ms=%u access=%s",
		 (unsigned long long)ts0, window, module, duration, period_ms,
		 win_is_fake ? "file-fake" : "mmap");
	if (!quiet)
		fprintf(stderr, "omo-h2dsampler v2: waiting for module %s (window 0x%08x, mmap)\n",
			module, window);

	/* Wait for the vendor platform module and for the SR ring to be programmed.
	 * In the OMO_H2D_FAKE_WIN host-test mode there is no vendor module: treat it as
	 * loaded so the window/ring logic itself is exercised. */
	while (!g_stop && now_ms() - ts0 < (uint64_t)wait_max * 1000u) {
		int loaded = win_is_fake ? 1 : module_loaded(module);
		uint32_t base = 0, depth = 0, prod = 0, cons = 0, ctrl = 0;

		if (loaded > 0) {
			read_channel_regs(0, &ctrl, &base, &depth, &prod, &cons);
			if (!probe_last_logged) {
				log_line("E ts_ms=%llu msg=win-probe base=0x%08x depth=0x%08x "
					 "prod=0x%08x cons=0x%08x ctrl=0x%08x ring=%s",
					 (unsigned long long)now_ms(), base, depth, prod, cons, ctrl,
					 ring_valid(base, depth) ? "yes" : "no");
				probe_last_logged = 1;
			}
			if (ring_valid(base, depth)) {
				ring_seen = 1;
				break;
			}
		}
		usleep(5000);
	}
	if (!ring_seen && !g_stop) {
		uint32_t base = 0, depth = 0, prod = 0, cons = 0, ctrl = 0;
		read_channel_regs(0, &ctrl, &base, &depth, &prod, &cons);
		log_line("E ts_ms=%llu msg=ring-not-seen wait_sec=%u module_loaded=%d "
			 "last=base=0x%08x depth=0x%08x prod=0x%08x win_err=%llu misreads=%llu",
			 (unsigned long long)now_ms(), wait_max,
			 win_is_fake ? 1 : module_loaded(module),
			 base, depth, prod, g_win_misreads, g_dram_misreads);
		fprintf(stderr, "omo-h2dsampler: ring not seen within %u s (last base=0x%08x depth=0x%08x)\n",
			wait_max, base, depth);
		fclose(log_fp);
		if (mem_fd >= 0)
			close(mem_fd);
		munmap(win_map, WIN_MAP_SIZE);
		return 4;
	}
	ts_ring = now_ms();
	log_line("E ts_ms=%llu msg=ring-seen dt_wait_ms=%llu",
		 (unsigned long long)ts_ring, (unsigned long long)(ts_ring - ts0));
	if (!quiet)
		fprintf(stderr, "omo-h2dsampler: ring seen after %llu ms; capturing %u s\n",
			(unsigned long long)(ts_ring - ts0), duration);

	while (!g_stop) {
		uint64_t now = now_ms();

		if (now - ts_ring > (uint64_t)duration * 1000u)
			break;
		if (stop_path && *stop_path && stop_file_exists(stop_path))
			break;

		for (i = 0; i < SR_CHANNELS; i++) {
			uint32_t ctrl, base, depth, prod, cons;

			read_channel_regs(i, &ctrl, &base, &depth, &prod, &cons);
			if (!base || !depth)
				continue;
			if (ring_valid(base, depth)) {
				if ((now - last_state) > 1000u) {
					log_line("R ts_ms=%llu ch=%d ctrl=0x%08x base=0x%08x depth=0x%x "
						 "prod=0x%08x cons=0x%08x",
						 (unsigned long long)now, i, ctrl, base, depth, prod, cons);
				}
				drain_channel(i, base, depth, prod, &st[i]);
			}
		}
		if (now - last_state > 1000u) {
			last_state = now;
			log_line("E ts_ms=%llu msg=heartbeat misreads=%llu win_err=%llu",
				 (unsigned long long)now, g_dram_misreads, g_win_misreads);
		}
		usleep(period_ms * 1000u);
	}
	log_line("E ts_ms=%llu msg=stopped", (unsigned long long)now_ms());
	if (!quiet)
		fprintf(stderr, "omo-h2dsampler: done; log=%s\n", log_path);
	fclose(log_fp);
	if (mem_fd >= 0)
		close(mem_fd);
	munmap(win_map, WIN_MAP_SIZE);
	return 0;
}
