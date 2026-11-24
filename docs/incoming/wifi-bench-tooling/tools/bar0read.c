// SPDX-License-Identifier: GPL-2.0-only
/*
 * bar0read - read-only BAR0 word reads for the Hi5622V100 Wi-Fi bench.
 *
 * Lane: wifi-bench-tooling (task-108 follow-up).  HOST-ONLY build; the static
 * ARM binary is deployed to the box as /root/hi5622-bar0read and used by the
 * protocol-mode device script to read firmware-RAM/registers through the BAR0
 * physical window.
 *
 * Why: the bench kernel has CONFIG_DEVMEM=y / CONFIG_STRICT_DEVMEM=n but the
 * image's busybox has no devmem applet, does not read() PCI MMIO through
 * /dev/mem (ARM __va() cannot map 0x5xxxxxxxx device addresses), and the PCI
 * sysfs resource0 file does not support read().  A tiny PROT_READ mmap of
 * /dev/mem is the only reliable userspace MMIO read path (this is what the
 * busybox devmem applet does internally).
 *
 * Usage:  bar0read <phys_addr> [<phys_addr> ..]      (hex, 4-byte aligned)
 * Output: one "0x%08x" line per address, in argument order.
 * Return: 0 ok, 2 usage/bad address, 3 refused (RC misc window), 4 /dev/mem,
 *         5 mmap failure.
 *
 * SAFETY: read-only by construction (O_RDONLY + PROT_READ, never write/mmap
 * shared write), refuses unaligned addresses and the RC misc window
 * 0x10160000..0x1016ffff (a read there panicked the box once; project hard
 * rule), and never touches the device mailbox/registers except the addresses
 * the caller passes.  The bench script only passes BAR0_PHYS + validated BAR0
 * offsets.
 */
#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_BYTES   4096UL
#define MAX_ADDRS    64
#define RC_MISC_LO   0x10160000UL
#define RC_MISC_HI   0x1016FFFFUL

static int parse_addr(const char *s, unsigned long *out)
{
	char *end = NULL;
	unsigned long v;

	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno != 0 || end == s || end == NULL || *end != '\0')
		return -1;
	*out = v;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned long addrs[MAX_ADDRS];
	int n, i, rc = 0;
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <phys_addr> [<phys_addr> ..]\n", argv[0]);
		return 2;
	}
	n = argc - 1;
	if (n > MAX_ADDRS) {
		fprintf(stderr, "too many addresses (max %d)\n", MAX_ADDRS);
		return 2;
	}
	for (i = 0; i < n; i++) {
		if (parse_addr(argv[i + 1], &addrs[i]) != 0) {
			fprintf(stderr, "bad address: %s\n", argv[i + 1]);
			return 2;
		}
		if (addrs[i] & 3UL) {
			fprintf(stderr, "unaligned address: 0x%08lx\n", addrs[i]);
			return 2;
		}
		if (addrs[i] >= RC_MISC_LO && addrs[i] <= RC_MISC_HI) {
			fprintf(stderr, "refused: RC misc window is never read (0x%08lx)\n",
				addrs[i]);
			return 3;
		}
	}

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "open /dev/mem: %s\n", strerror(errno));
		return 4;
	}

	for (i = 0; i < n; i++) {
		unsigned long page = addrs[i] & ~(PAGE_BYTES - 1);
		unsigned long delta = addrs[i] - page;
		uint32_t v;
		void *map;

		map = mmap(NULL, PAGE_BYTES, PROT_READ, MAP_SHARED, fd,
			   (off_t)page);
		if (map == MAP_FAILED) {
			fprintf(stderr, "mmap 0x%08lx: %s\n", page,
				strerror(errno));
			rc = 5;
			break;
		}
		memcpy(&v, (const char *)map + delta, sizeof(v));
		printf("0x%08x\n", v);
		munmap(map, PAGE_BYTES);
	}
	close(fd);
	return rc;
}
