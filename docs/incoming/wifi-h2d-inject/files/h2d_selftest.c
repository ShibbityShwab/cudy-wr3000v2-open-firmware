// SPDX-License-Identifier: GPL-2.0
/*
 * h2d_selftest.c - userspace selftest + small frame tool for the Hi5622V100
 * H2D injector (host-only; needs no PCIe device and no kernel headers).
 *
 * ORIGINAL code written by the wifi-h2d-inject lane.  It compiles the SAME
 * pure-C frame model the kernel module uses (files/pciet_h2d_frame.h), so
 * what is tested here is exactly the parser/builder/validator the on-target
 * sysfs injector runs.
 *
 * usage:
 *   h2d_selftest                    run the shared self-test suite
 *   h2d_selftest build <request>    print the frame h2d_inject would post
 *   h2d_selftest validate <hex>     validate a raw 12+ byte frame
 *
 * <request> is the sysfs h2d_inject line, e.g.
 *   "group=4 id=1 dst=0 payload=00000000"
 * <hex> is the h2d_raw line: 12+ bytes of hex (whitespace tolerated).
 *
 * exit status: 0 = OK / all cases passed, 1 = failure, 2 = usage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pciet_h2d_frame.h"

static char *join_args(int argc, char **argv)
{
	size_t len = 0;
	int i;
	char *out;

	for (i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	out = calloc(1, len + 1);
	if (!out)
		return NULL;
	for (i = 0; i < argc; i++) {
		if (i)
			strcat(out, " ");
		strcat(out, argv[i]);
	}
	return out;
}

static int tool_build(int argc, char **argv)
{
	struct h2d_request *req;
	u8 *frame;
	char err[160];
	char *text;
	size_t flen = 0;
	int rc;

	if (!argc) {
		fprintf(stderr, "build: missing request text\n");
		return 2;
	}
	text = join_args(argc, argv);
	req = calloc(1, sizeof(*req));
	frame = malloc(H2D_HCC_MAX_TOTAL);
	if (!text || !req || !frame) {
		fprintf(stderr, "build: out of memory\n");
		free(text);
		free(req);
		free(frame);
		return 1;
	}
	err[0] = '\0';
	rc = h2d_parse_request(text, strlen(text), req, err, sizeof(err));
	if (rc != H2D_OK) {
		fprintf(stderr, "build: request refused: %s (%s)\n",
			h2d_strerror(rc), err);
		free(text);
		free(req);
		free(frame);
		return 1;
	}
	rc = h2d_build_frame(req, frame, H2D_HCC_MAX_TOTAL, &flen,
			     err, sizeof(err));
	if (rc != H2D_OK) {
		fprintf(stderr, "build: frame refused: %s (%s)\n",
			h2d_strerror(rc), err);
		free(text);
		free(req);
		free(frame);
		return 1;
	}
	rc = h2d_validate_frame(frame, flen, err, sizeof(err));
	if (rc != H2D_OK) {
		fprintf(stderr, "build: built frame failed validation: %s (%s)\n",
			h2d_strerror(rc), err);
		free(text);
		free(req);
		free(frame);
		return 1;
	}
	printf("request: %s\n", text);
	printf("fields : group=%u id=%u dst=%u a=%u b=%u c=%u payload=%u B\n",
	       req->group, req->id, req->dst, req->field_a, req->field_b,
	       req->field_c, req->payload_len);
	printf("frame  : %zu B total, SR node word1 would be 0x%08x\n",
	       flen, (unsigned)(((unsigned)flen << 16) | 0x6d2bu));
	{
		size_t i;

		printf("hex    : ");
		for (i = 0; i < flen; i++)
			printf("%02x", frame[i]);
		printf("\n");
	}
	free(text);
	free(req);
	free(frame);
	return 0;
}

static int tool_validate(int argc, char **argv)
{
	u8 *frame;
	char err[160];
	char *text;
	size_t flen = 0;
	int rc;

	if (!argc) {
		fprintf(stderr, "validate: missing frame hex\n");
		return 2;
	}
	text = join_args(argc, argv);
	frame = calloc(1, H2D_HCC_MAX_TOTAL);
	if (!text || !frame) {
		fprintf(stderr, "validate: out of memory\n");
		free(text);
		free(frame);
		return 1;
	}
	err[0] = '\0';
	rc = h2d_parse_raw_frame(text, strlen(text), frame, H2D_HCC_MAX_TOTAL,
				 &flen, err, sizeof(err));
	if (rc != H2D_OK) {
		fprintf(stderr, "validate: frame refused: %s (%s)\n",
			h2d_strerror(rc), err);
		free(text);
		free(frame);
		return 1;
	}
	printf("valid  : %zu B total (%zu B payload)\n", flen,
	       flen - H2D_HCC_HDR_LEN);
	printf("hdr    : group=0x%x rsrc/dst=0x%x a=0x%02x b=0x%02x "
	       "len=0x%04x id=0x%04x c=0x%02x dst=0x%x tag=0x%04x\n",
	       frame[0] & 0x0f, frame[1] >> 4, frame[2], frame[3],
	       frame[4] | (frame[5] << 8), frame[6] | (frame[7] << 8),
	       frame[8], frame[9], frame[10] | (frame[11] << 8));
	free(text);
	free(frame);
	return 0;
}

static int run_selftest(void)
{
	struct h2d_selftest_buf *b;
	char *log;
	u32 passed = 0, failed = 0;
	int rc;

	b = calloc(1, sizeof(*b));
	log = calloc(1, 16 * 1024);
	if (!b || !log) {
		fprintf(stderr, "selftest: out of memory\n");
		free(b);
		free(log);
		return 1;
	}
	rc = h2d_run_selftest(b, log, 16 * 1024, &passed, &failed);
	fputs(log, stdout);
	printf("h2d selftest: %s (%u passed, %u failed)\n",
	       rc == H2D_OK ? "OK" : "FAILED", passed, failed);
	free(b);
	free(log);
	return rc == H2D_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return run_selftest();
	if (strcmp(argv[1], "build") == 0)
		return tool_build(argc - 2, argv + 2);
	if (strcmp(argv[1], "validate") == 0)
		return tool_validate(argc - 2, argv + 2);
	fprintf(stderr, "usage: %s [build <request> | validate <frame-hex>]\n",
		argv[0]);
	return 2;
}
