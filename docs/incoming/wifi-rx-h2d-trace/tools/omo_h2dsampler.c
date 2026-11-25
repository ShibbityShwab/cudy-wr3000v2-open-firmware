/* omo_h2dsampler - read-only host->device (H2D) trace sampler for the Cudy WR3000 v2.0
 * stock (pair-A) image, lane rx-h2d-trace / task-176.
 *
 * WHAT IT DOES (READ-ONLY):
 *   The vendor's H2D messages are HCC frames in host DRAM, posted as 8-byte nodes into the
 *   ETE SR ring. This tool polls the SR channel producer registers (through the endpoint's
 *   BAR0 region-3 window, which on this board is a host-physical MMIO window at 0x403b8000 +
 *   (CA - 0x40000000); the ETE window CA 0x4003a000 is therefore host 0x403f2000), and for
 *   every new node reads the 8-byte node + the message buffer + the 12-byte HCC header, and
 *   appends one line per message to a log file.
 *
 *   It performs ONLY: pread() on /dev/mem (physical reads), writes to its own log file, and
 *   stat()/open() of /proc/modules + a stop file. No device writes, no module loads, no flash.
 *
 * USAGE (on the stock root shell; run from a one-shot init aid):
 *   omo-h2dsampler -o /overlay/h2d-trace.log -t 180 -r 20
 *   options:
 *     -o PATH   log file (append). default: /overlay/h2d-trace.log
 *     -t SEC    capture duration after the ring appears. default: 180
 *     -r MS     poll period. default: 20
 *     -w PHYS   ETE window host physical address. default: 0x403f2000
 *     -m NAME   module to wait for. default: hi5622v100_plat
 *     -s PATH   stop file; stop when it exists. default: /overlay/h2d-trace.stop
 *     -W SEC    max wait for the module/ring before giving up. default: 120
 *     -q        quiet (only the log file)
 *
 * LOG FORMAT (version 1):
 *   # omo_h2dsampler v1 ts0=... window=... module=...
 *   R ts_ms=... ch=... ctrl=... base=... depth=... prod=... cons=...
 *   M ts_ms=... ch=... idx=... prime=... node=0x... len=... flags=0x... total=... group=...
 *     bid=0x... vap=... chip=... id=... dst=... hex=<full frame hex>
 *   E ts_ms=... msg=...
 *
 * The M record carries the FULL frame bytes; the host decoder (tools/decode_h2d.py) does the
 * validation/classification/diff. See docs/incoming/wifi-rx-h2d-trace/H2D-FORMATS.md.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ETE_DEFAULT_WINDOW  0x403f2000u   /* CA 0x4003a000 through BAR0 region-3 */
#define SR_BLOCK_OFF        0x400u        /* SR0 block inside the ETE window */
#define SR_STRIDE           0x50u
#define SR_CHANNELS         3
#define REG_CTRL            0x08u
#define REG_BASE            0x10u
#define REG_DEPTH           0x14u
#define REG_PROD            0x18u
#define REG_CONS            0x1cu
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

static int mem_fd = -1;
static FILE *log_fp;
static int quiet;

static int phys_read(uint32_t phys, void *buf, size_t len)
{
	ssize_t n;
	if (mem_fd < 0)
		return -1;
	n = pread(mem_fd, buf, len, (off_t)phys);
	return (n == (ssize_t)len) ? 0 : -1;
}

static int read32(uint32_t phys, uint32_t *out)
{
	return phys_read(phys, out, sizeof(*out));
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

	if (depth == 0 || depth > 1024)
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

int main(int argc, char **argv)
{
	const char *log_path = "/overlay/h2d-trace.log";
	const char *module = "hi5622v100_plat";
	const char *stop_path = "/overlay/h2d-trace.stop";
	uint32_t window = ETE_DEFAULT_WINDOW;
	unsigned duration = 180, period_ms = 20, wait_max = 120;
	uint64_t ts0, ts_ring = 0, last_state = 0, last_report = 0;
	struct chan_state st[SR_CHANNELS];
	int opt, i, ring_seen = 0;

	memset(st, 0, sizeof(st));
	while ((opt = getopt(argc, argv, "o:t:r:w:m:s:W:qh")) != -1) {
		switch (opt) {
		case 'o': log_path = optarg; break;
		case 't': duration = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'r': period_ms = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'w': window = (uint32_t)strtoul(optarg, NULL, 0); break;
		case 'm': module = optarg; break;
		case 's': stop_path = optarg; break;
		case 'W': wait_max = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'q': quiet = 1; break;
		default:
			fprintf(stderr,
				"usage: %s [-o log] [-t sec] [-r ms] [-w phys] [-m module] "
				"[-s stopfile] [-W waitsec] [-q]\n", argv[0]);
			return 2;
		}
	}
	if (period_ms < 1)
		period_ms = 1;

	log_fp = fopen(log_path, "a");
	if (!log_fp) {
		fprintf(stderr, "omo-h2dsampler: cannot open %s: %s\n", log_path, strerror(errno));
		return 3;
	}
	setvbuf(log_fp, NULL, _IOLBF, 0);

	mem_fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (mem_fd < 0) {
		log_line("E ts_ms=%llu msg=devmem-open-failed errno=%d", (unsigned long long)now_ms(), errno);
		fprintf(stderr, "omo-h2dsampler: /dev/mem open failed: %s\n", strerror(errno));
		fclose(log_fp);
		return 3;
	}

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	ts0 = now_ms();
	log_line("# omo_h2dsampler v1 ts0=%llu window=0x%08x module=%s duration=%u period_ms=%u",
		 (unsigned long long)ts0, window, module, duration, period_ms);
	if (!quiet)
		fprintf(stderr, "omo-h2dsampler: waiting for module %s (window 0x%08x)\n",
			module, window);

	/* Wait for the vendor platform module and for the SR ring to be programmed. */
	while (!g_stop && now_ms() - ts0 < (uint64_t)wait_max * 1000u) {
		int loaded = module_loaded(module);
		uint32_t base = 0, depth = 0;

		if (loaded > 0) {
			read32(window + SR_BLOCK_OFF + REG_BASE, &base);
			read32(window + SR_BLOCK_OFF + REG_DEPTH, &depth);
			if (base != 0 && depth != 0) {
				ring_seen = 1;
				break;
			}
		}
		usleep(5000);
	}
	if (!ring_seen && !g_stop) {
		log_line("E ts_ms=%llu msg=ring-not-seen wait_sec=%u module_loaded=%d",
			 (unsigned long long)now_ms(), wait_max, module_loaded(module));
		fprintf(stderr, "omo-h2dsampler: ring not seen within %u s\n", wait_max);
		fclose(log_fp);
		close(mem_fd);
		return 4;
	}
	ts_ring = now_ms();
	log_line("E ts_ms=%llu msg=ring-seen dt_wait_ms=%llu", (unsigned long long)ts_ring,
		 (unsigned long long)(ts_ring - ts0));
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
			uint32_t off = window + SR_BLOCK_OFF + (uint32_t)i * SR_STRIDE;
			uint32_t ctrl, base, depth, prod, cons;

			if (read32(off + REG_CTRL, &ctrl) ||
			    read32(off + REG_BASE, &base) ||
			    read32(off + REG_DEPTH, &depth) ||
			    read32(off + REG_PROD, &prod) ||
			    read32(off + REG_CONS, &cons))
				continue;
			if (!base || !depth)
				continue;
			if ((now - last_state) > 1000u) {
				log_line("R ts_ms=%llu ch=%d ctrl=0x%08x base=0x%08x depth=0x%x "
					 "prod=0x%08x cons=0x%08x",
					 (unsigned long long)now, i, ctrl, base, depth, prod, cons);
			}
			drain_channel(i, base, depth, prod, &st[i]);
		}
		if (now - last_state > 1000u) {
			last_state = now;
			log_line("E ts_ms=%llu msg=heartbeat", (unsigned long long)now);
		}
		usleep(period_ms * 1000u);
	}
	(void)last_report;
	log_line("E ts_ms=%llu msg=stopped", (unsigned long long)now_ms());
	if (!quiet)
		fprintf(stderr, "omo-h2dsampler: done; log=%s\n", log_path);
	fclose(log_fp);
	close(mem_fd);
	return 0;
}
