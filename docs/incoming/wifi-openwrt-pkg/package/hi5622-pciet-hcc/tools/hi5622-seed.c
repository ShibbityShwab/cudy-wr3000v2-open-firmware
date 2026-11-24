// SPDX-License-Identifier: GPL-2.0-only
/*
 * hi5622-seed - guarded custom-block pre-seed helper for the clean-room
 * Hi5622V100 bench driver (Cudy WR3000 v2.0 / Hi5671Y PCIe link).
 *
 * The vendor host writes two configuration blocks into the Wi-Fi firmware's
 * RAM before the CPU release word:
 *
 *   platform descriptor   CPU 0x001B2800  BAR0 0x86A800  24 words (0x060 B)
 *                         (TCM handoff bounds + boot config; entry A reads it)
 *   Wi-Fi PHY/radio block CPU 0x001B2860  BAR0 0x86A860  618 words (0x9A8 B)
 *                         (vendor-built from device INI data)
 *
 * BAR0 offset = CPU address + 0x006B8000 (region-5 firmware-RAM window).
 *
 * PUBLICATION RULE: the block CONTENTS are vendor-derived local evidence.
 * This tool contains addresses, sizes and formats only; the word files it
 * reads/writes are captured locally (dump mode, read-only) and are NEVER
 * committed to the repository.  No firmware image or vendor byte is embedded
 * here.
 *
 * SAFETY:
 *   * write mode refuses when the handshake page (BAR0 0x877000, CA 0x1BF000)
 *     shows the image already ran (page +8 == 2, the CPU1 S1 ack, or page +0
 *     in the entry-A range 0x8680xxxx) unless --force is given explicitly;
 *   * write mode verifies every word by read-back and fails on mismatch;
 *   * the tool touches only the two block windows and the read-only handshake
 *     page: it never writes CA 0x400392F0 (out[5]) and never touches the CECE
 *     gate or the release word;
 *   * the pre-release page on a clean boot reads 0x00000007/0x00000003/...;
 *     those values are allowed.
 *
 * Usage:
 *   hi5622-seed dump  [--bdf BDF] [--platform FILE] [--wifi FILE]
 *   hi5622-seed write [--bdf BDF] [--platform FILE] [--wifi FILE] [--force]
 *
 *   dump   read-only; writes the word-list files (or prints to stdout when no
 *          file is given).  Word-list format: one word per line, either
 *          "INDEX=0xVALUE" or bare "0xVALUE" in index order; '#' comments and
 *          blank lines are ignored.
 *   write  parse the word-list file(s), validate the exact word count, refuse
 *          if the boot page says the image already ran (unless --force),
 *          write the block(s) in ascending word order, then read back.
 *
 *   --bdf  PCI endpoint BDF; default 0000:00:00.0 (the bench endpoint).
 *
 * Exit: 0 success, 1 runtime error, 2 usage.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_BDF		"0000:00:00.0"

#define BLOCK_PLAT_BAR0		0x86a800u
#define BLOCK_PLAT_WORDS	24u
#define BLOCK_WIFI_BAR0		0x86a860u
#define BLOCK_WIFI_WORDS	618u

#define PAGE_BAR0		0x877000u	/* CA 0x1BF000 */
#define PAGE_ACK		2u		/* +8 == 2: CPU1 S1 completed */
#define PAGE_ENTRY_A_MASK	0xffff0000u
#define PAGE_ENTRY_A_SIG	0x86800000u	/* entry A progress words */

#define MAX_WORDS		BLOCK_WIFI_WORDS

struct block {
	const char *name;
	uint32_t bar0;
	unsigned words;
	const char *file;
};

struct bar0 {
	int fd;
	void *map;
	size_t size;
};

static void usage(FILE *out)
{
	fprintf(out,
"hi5622-seed - guarded local-only custom-block helper (no vendor bytes)\n"
"\n"
"Usage:\n"
"  hi5622-seed dump  [--bdf BDF] [--platform FILE] [--wifi FILE]\n"
"  hi5622-seed write [--bdf BDF] [--platform FILE] [--wifi FILE] [--force]\n"
"\n"
"defaults: --bdf %s\n"
"blocks:   platform BAR0 0x%06x %u words | wifi BAR0 0x%06x %u words\n"
"\n"
"Word-list format: \"INDEX=0xVALUE\" or bare \"0xVALUE\" in index order;\n"
"'#' comments and blank lines ignored.  Block data is LOCAL EVIDENCE ONLY\n"
"and must never be committed (project publication rule).\n",
		DEFAULT_BDF, BLOCK_PLAT_BAR0, BLOCK_PLAT_WORDS,
		BLOCK_WIFI_BAR0, BLOCK_WIFI_WORDS);
}

static uint32_t rd32(const struct bar0 *b, uint32_t off)
{
	return *(volatile uint32_t *)((char *)b->map + off);
}

static void wr32(const struct bar0 *b, uint32_t off, uint32_t val)
{
	*(volatile uint32_t *)((char *)b->map + off) = val;
}

/* BAR0 size from /sys/bus/pci/devices/<bdf>/resource line 0. */
static int bar0_size(const char *bdf, size_t *size)
{
	char path[256], line[256];
	unsigned long long start, end, flags;
	FILE *f;

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource", bdf);
	f = fopen(path, "r");
	if (!f)
		return -1;
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	if (sscanf(line, "0x%llx 0x%llx 0x%llx", &start, &end, &flags) != 3)
		return -1;
	if (end <= start)
		return -1;
	*size = (size_t)(end - start + 1);
	return 0;
}

static int bar0_open(struct bar0 *b, const char *bdf, int writable)
{
	char path[256];
	struct stat st;
	int prot = PROT_READ | (writable ? PROT_WRITE : 0);

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0", bdf);
	b->fd = open(path, writable ? (O_RDWR | O_SYNC) : (O_RDONLY | O_SYNC));
	if (b->fd < 0) {
		fprintf(stderr, "error: open %s: %s\n", path, strerror(errno));
		return -1;
	}
	if (fstat(b->fd, &st) == 0 && st.st_size > 0)
		b->size = (size_t)st.st_size;
	else if (bar0_size(bdf, &b->size) != 0) {
		fprintf(stderr, "error: cannot determine BAR0 size of %s\n", bdf);
		close(b->fd);
		return -1;
	}
	b->map = mmap(NULL, b->size, prot, MAP_SHARED, b->fd, 0);
	if (b->map == MAP_FAILED) {
		fprintf(stderr, "error: mmap %s (%zu B): %s\n", path, b->size,
			strerror(errno));
		b->map = NULL;
		close(b->fd);
		return -1;
	}
	return 0;
}

static void bar0_close(struct bar0 *b)
{
	if (b->map)
		munmap(b->map, b->size);
	if (b->fd >= 0)
		close(b->fd);
	b->map = NULL;
	b->fd = -1;
}

static int endpoint_present(const char *bdf)
{
	char path[256], buf[32];
	FILE *f;

	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/vendor", bdf);
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	printf("endpoint %s vendor=%s", bdf, buf);
	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/device", bdf);
	f = fopen(path, "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f))
			printf("device=%s", buf);
		fclose(f);
	}
	printf("\n");
	return 1;
}

/* Refuse to write blocks when the firmware image already ran or is running. */
static int boot_page_guard(const struct bar0 *b, int force)
{
	uint32_t w0 = rd32(b, PAGE_BAR0 + 0x00);
	uint32_t w1 = rd32(b, PAGE_BAR0 + 0x04);
	uint32_t w2 = rd32(b, PAGE_BAR0 + 0x08);
	uint32_t w3 = rd32(b, PAGE_BAR0 + 0x0c);

	printf("handshake page +0=0x%08x +4=0x%08x +8=0x%08x +c=0x%08x\n",
	       w0, w1, w2, w3);
	if (w2 == PAGE_ACK) {
		fprintf(stderr,
			"REFUSE: handshake page +8 == %u (CPU1 S1 done; the image ran and already consumed/overwrote the blocks)\n",
			PAGE_ACK);
	} else if ((w0 & PAGE_ENTRY_A_MASK) == PAGE_ENTRY_A_SIG) {
		fprintf(stderr,
			"REFUSE: handshake page +0 == 0x%08x (entry A already progressed; pre-seed must be done before the release)\n",
			w0);
	} else {
		return 0;
	}
	if (!force) {
		fprintf(stderr,
			"refusing to write custom blocks; --force overrides for a diagnostic run only\n");
		return -1;
	}
	fprintf(stderr, "WARNING: --force given: writing blocks after the image progressed\n");
	return 0;
}

static int parse_word_file(const struct block *blk, uint32_t *out)
{
	unsigned char seen[MAX_WORDS];
	char line[256];
	unsigned long idx, val;
	unsigned last = 0, got = 0;
	char *eq, *end;
	FILE *f;

	if (!blk->file) {
		fprintf(stderr, "error: no file given for the %s block\n", blk->name);
		return -1;
	}
	f = fopen(blk->file, "r");
	if (!f) {
		fprintf(stderr, "error: open %s: %s\n", blk->file, strerror(errno));
		return -1;
	}
	memset(out, 0, blk->words * sizeof(*out));
	memset(seen, 0, sizeof(seen));
	while (fgets(line, sizeof(line), f)) {
		char *s = line;

		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == '#' || *s == '\n' || *s == '\0')
			continue;
		eq = strchr(s, '=');
		if (eq) {
			*eq = '\0';
			errno = 0;
			idx = strtoul(s, &end, 0);
			if (errno || end == s || (*end && *end != ' ' && *end != '\t')) {
				fprintf(stderr, "error: %s: bad index '%s'\n",
					blk->file, s);
				goto err;
			}
			s = eq + 1;
		} else {
			idx = last;
		}
		errno = 0;
		val = strtoul(s, &end, 0);
		if (errno || end == s || val > 0xffffffffUL) {
			fprintf(stderr, "error: %s: bad value line '%s'\n",
				blk->file, line);
			goto err;
		}
		if (idx >= blk->words) {
			fprintf(stderr, "error: %s: index %lu out of range (max %u)\n",
				blk->file, idx, blk->words - 1);
			goto err;
		}
		if (seen[idx]) {
			fprintf(stderr, "error: %s: duplicate index %lu\n",
				blk->file, idx);
			goto err;
		}
		seen[idx] = 1;
		got++;
		out[idx] = (uint32_t)val;
		last = idx + 1;
	}
	fclose(f);
	if (got != blk->words) {
		fprintf(stderr,
			"error: %s: %u/%u words present (need exactly %u)\n",
			blk->file, got, blk->words, blk->words);
		return -1;
	}
	return 0;
err:
	fclose(f);
	return -1;
}

static void dump_header(const char *title)
{
	printf("== %s ==\n", title);
}

static int dump_block(const struct bar0 *b, const struct block *blk)
{
	unsigned i;
	FILE *f = NULL;

	dump_header(blk->name);
	if (blk->file) {
		f = fopen(blk->file, "w");
		if (!f) {
			fprintf(stderr, "error: create %s: %s\n", blk->file,
				strerror(errno));
			return -1;
		}
	}
	for (i = 0; i < blk->words; i++) {
		uint32_t v = rd32(b, blk->bar0 + 4 * i);

		if (f)
			fprintf(f, "%u=0x%08x\n", i, v);
		else
			printf("%u=0x%08x\n", i, v);
	}
	if (f) {
		fclose(f);
		printf("wrote %u words to %s (LOCAL EVIDENCE ONLY - do not commit)\n",
		       blk->words, blk->file);
	}
	return 0;
}

static int read_blocks(const struct bar0 *b, struct block *blocks, int n)
{
	int i, ret = 0;

	for (i = 0; i < n; i++) {
		struct block *blk = &blocks[i];
		uint32_t words[MAX_WORDS];
		unsigned w;

		if (!blk->file)
			continue;
		if (parse_word_file(blk, words))
			return -1;
		printf("== write %s: %u words to BAR0 0x%06x ==\n",
		       blk->name, blk->words, blk->bar0);
		for (w = 0; w < blk->words; w++)
			wr32(b, blk->bar0 + 4 * w, words[w]);
		/* read back and verify the whole block */
		for (w = 0; w < blk->words; w++) {
			uint32_t got = rd32(b, blk->bar0 + 4 * w);

			if (got != words[w]) {
				fprintf(stderr,
					"error: %s word %u readback 0x%08x != 0x%08x\n",
					blk->name, w, got, words[w]);
				ret = -1;
				break;
			}
		}
		if (ret)
			break;
		printf("readback: %u/%u words OK\n", blk->words, blk->words);
	}
	return ret;
}

int main(int argc, char **argv)
{
	struct block blocks[2] = {
		{ .name = "platform", .bar0 = BLOCK_PLAT_BAR0,
		  .words = BLOCK_PLAT_WORDS },
		{ .name = "wifi", .bar0 = BLOCK_WIFI_BAR0,
		  .words = BLOCK_WIFI_WORDS },
	};
	const char *bdf = DEFAULT_BDF;
	const char *mode;
	int i, force = 0, dump, nblocks = 0, any = 0, ret;
	struct bar0 b = { .fd = -1 };

	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	mode = argv[1];
	if (!strcmp(mode, "-h") || !strcmp(mode, "--help") || !strcmp(mode, "help")) {
		usage(stdout);
		return 0;
	}
	if (strcmp(mode, "dump") && strcmp(mode, "write")) {
		fprintf(stderr, "error: unknown mode '%s'\n\n", mode);
		usage(stderr);
		return 2;
	}
	dump = !strcmp(mode, "dump");
	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--bdf") && i + 1 < argc) {
			bdf = argv[++i];
		} else if (!strcmp(argv[i], "--platform") && i + 1 < argc) {
			blocks[0].file = argv[++i];
			any = 1;
		} else if (!strcmp(argv[i], "--wifi") && i + 1 < argc) {
			blocks[1].file = argv[++i];
			any = 1;
		} else if (!strcmp(argv[i], "--force") && !dump) {
			force = 1;
		} else {
			fprintf(stderr, "error: bad argument '%s'\n\n", argv[i]);
			usage(stderr);
			return 2;
		}
	}
	if (!dump && !any) {
		fprintf(stderr, "error: write mode needs --platform FILE and/or --wifi FILE\n");
		return 2;
	}
	if (!endpoint_present(bdf))
		return 1;
	if (bar0_open(&b, bdf, !dump)) {
		bar0_close(&b);
		return 1;
	}
	if (b.size < BLOCK_WIFI_BAR0 + BLOCK_WIFI_WORDS * 4 ||
	    b.size < PAGE_BAR0 + 0x10) {
		fprintf(stderr,
			"error: BAR0 size 0x%zx too small for the block/page windows\n",
			b.size);
		bar0_close(&b);
		return 1;
	}

	for (i = 0; i < 2; i++) {
		if (blocks[i].file)
			nblocks++;
	}
	if (dump) {
		if (!nblocks) {
			/* no file targets: print both blocks to stdout */
			blocks[0].file = NULL;
			blocks[1].file = NULL;
			ret = dump_block(&b, &blocks[0]);
			if (!ret)
				ret = dump_block(&b, &blocks[1]);
		} else {
			ret = 0;
			for (i = 0; i < 2 && !ret; i++)
				if (blocks[i].file)
					ret = dump_block(&b, &blocks[i]);
		}
	} else {
		ret = boot_page_guard(&b, force);
		if (!ret)
			ret = read_blocks(&b, blocks, 2);
	}
	bar0_close(&b);
	return ret ? 1 : 0;
}
