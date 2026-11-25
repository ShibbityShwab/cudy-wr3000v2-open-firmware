// SPDX-License-Identifier: GPL-2.0
/*
 * rc2_frame_selftest.c - host KAT for the RC-2 rate-control wire builder.
 *
 * Asserts the byte-exact 39-B SET rate_mode auto frame (the wifi-rate-ctl
 * lane's decoded blob 91 01 00 0d 00 00 00 00 01 00 00 on the bench-proven
 * g0 id2 / key 0x0101 carrier), the carrier header fields, the blob/wire
 * equality, the current base's validator acceptance and the fail-closed
 * refusals of the builder.
 *
 * No device, no kernel tree, no vendor artifact.  Build (see
 * tools/run-host-tests.sh):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -I<tree> \
 *      rc2_frame_selftest.c hi5622_rate_ctl_logic.c hi5622_rate_ctl_wire.c \
 *      h2d_frames.c
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"
#include "hi5622_rate_ctl_logic.h"
#include "hi5622_rate_ctl_wire.h"

static int checks_total;
static int checks_fail;

static void check(const char *name, int cond)
{
	checks_total++;
	if (!cond) {
		checks_fail++;
		printf("FAIL %s\n", name);
	}
}

/* The pinned wire of the decoded SET rate_mode auto command (REPORT sec 3):
 * 12-byte HCC header | 8-byte token | key 0x0101 | len 11 | alg_cfg blob |
 * 4-byte zero tail. */
static const unsigned char k_rate_mode_auto_wire[39] = {
	0x00, 0x01, 0x00, 0x04, 0x27, 0x00, 0x02, 0x00,
	0x00, 0x00, 0x5a, 0x5a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x01, 0x0b, 0x00,
	0x91, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
};
/* the decoded 11-byte alg_cfg blob (REPORT sec 3 / TABLES) */
static const unsigned char k_rate_mode_auto_blob[11] = {
	0x91, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00,
};

int main(void)
{
	struct rc_req req;
	unsigned char wire[RC_WIRE_MAX_LEN];
	unsigned char blob[RC_ALG_MAX_BLOB];
	size_t blob_len = 0, wire_len = 0;
	struct h2d_frame f;
	enum h2d_err err = H2D_OK;
	const struct h2d_fw_handler *handler;
	int i;
	unsigned differ = 0;

	/* ---- 1. the byte-exact SET rate_mode auto frame ---- */
	check("builder req SET auto", rc_req_rate_mode(&req, RC_RATE_MODE_AUTO) == 0);
	check("blob build", rc_blob_build(&req, blob, sizeof(blob), &blob_len) == 0 &&
			    blob_len == sizeof(k_rate_mode_auto_blob) &&
			    memcmp(blob, k_rate_mode_auto_blob, blob_len) == 0);
	check("wire build", rc_wire_build_req(&req, RC_CFG_FB_DEFAULT, wire,
					      sizeof(wire), &wire_len) == 0);
	check("wire len 39", wire_len == sizeof(k_rate_mode_auto_wire));
	check("wire BYTE-EXACT KAT",
	      memcmp(wire, k_rate_mode_auto_wire, sizeof(k_rate_mode_auto_wire)) == 0);
	if (memcmp(wire, k_rate_mode_auto_wire, sizeof(k_rate_mode_auto_wire)) != 0) {
		printf("  got  ");
		for (i = 0; i < (int)wire_len; i++)
			printf("%02x ", wire[i]);
		printf("\n  want ");
		for (i = 0; i < (int)sizeof(k_rate_mode_auto_wire); i++)
			printf("%02x ", k_rate_mode_auto_wire[i]);
		printf("\n");
	}

	/* ---- 2. carrier header fields (decoded contract) ---- */
	check("hdr group 0", (wire[0] & 0x0fu) == 0u);
	check("hdr alloc/dst 0x01", wire[1] == 0x01u);
	check("hdr field A 0", wire[2] == 0x00u);
	check("hdr field B = 4 (module 4)", wire[3] == RC_CFG_FB_DEFAULT);
	check("hdr total 0x27 (39)", (wire[4] | (wire[5] << 8)) == 39u);
	check("hdr id 2", (wire[6] | (wire[7] << 8)) == 2u);
	check("hdr field C 0", wire[8] == 0x00u);
	check("hdr dst 0", wire[9] == 0x00u);
	check("hdr tag 0x5a5a", (wire[10] | (wire[11] << 8)) == 0x5a5au);
	check("token 8 zero", memcmp(wire + 12, "\0\0\0\0\0\0\0\0", 8) == 0);
	check("key 0x0101", (wire[20] | (wire[21] << 8)) == RC_CFG_KEY);
	check("len field = blob len 11", (wire[22] | (wire[23] << 8)) == 11u);
	check("blob ON THE WIRE", memcmp(wire + 24, blob, blob_len) == 0);
	check("tail 4 zero", memcmp(wire + 24 + blob_len, "\0\0\0\0", 4) == 0);
	check("wire len == carrier + blob",
	      wire_len == RC_WIRE_CARRIER_OVERHEAD + blob_len);

	/* ---- 3. the current base's validator accepts the frame ---- */
	check("h2d_frame_decode transport+firmware+handler",
	      h2d_frame_decode(wire, wire_len, &f,
			       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
			       H2D_VF_HANDLER, &err) == 0);
	check("decoded group/id", f.group == 0u && f.id == 2u);
	check("decoded payload_len 27", f.payload_len == 27u);
	handler = h2d_fw_handler_lookup(f.group, f.id);
	check("firmware handler present", handler != NULL &&
					   handler->handler_ca != 0u);

	/* ---- 4. the module-context selector is the only fb-visible byte ---- */
	{
		unsigned char wire0[RC_WIRE_MAX_LEN];
		size_t len0 = 0;

		check("fb=0 wire builds",
		      rc_wire_build(blob, (uint16_t)blob_len, 0u, wire0,
				    sizeof(wire0), &len0) == 0 && len0 == wire_len);
		for (i = 0; i < (int)wire_len; i++)
			if (wire[i] != wire0[i])
				differ++;
		check("fb=0 differs only at byte 3", differ == 1u &&
						      wire0[3] == 0u);
	}

	/* ---- 5. GET rate_mode is a structurally identical frame ---- */
	check("builder GET rate_mode", rc_req_rate_mode_get(&req) == 0);
	{
		unsigned char wg[RC_WIRE_MAX_LEN];
		size_t lg = 0;

		check("GET wire builds", rc_wire_build_req(&req,
			RC_CFG_FB_DEFAULT, wg, sizeof(wg), &lg) == 0 && lg == 39u);
		check("GET type byte = 1 (wire[26])", wg[26] == 0x01u);
		check("GET len field 11", (wg[22] | (wg[23] << 8)) == 11u);
	}

	/* ---- 6. fail-closed refusals ---- */
	check("refuse fb > 0x12",
	      rc_wire_build(blob, (uint16_t)blob_len, 0x13u, wire, sizeof(wire),
			    &wire_len) != 0);
	check("refuse short blob", rc_wire_build(blob, 9u, 4u, wire, sizeof(wire),
						 &wire_len) != 0);
	check("refuse oversized blob", rc_wire_build(blob, 271u, 4u, wire,
						     sizeof(wire), &wire_len) != 0);
	check("refuse small cap", rc_wire_build(blob, (uint16_t)blob_len, 4u, wire,
						38u, &wire_len) != 0);
	check("refuse NULL wire", rc_wire_build(blob, (uint16_t)blob_len, 4u, NULL,
						sizeof(wire), &wire_len) != 0);
	check("refuse NULL out_len", rc_wire_build(blob, (uint16_t)blob_len, 4u,
						   wire, sizeof(wire), NULL) != 0);
	check("refuse NULL req", rc_wire_build_req(NULL, 4u, wire, sizeof(wire),
						   &wire_len) != 0);

	printf("rc2_frame_selftest: %d passed, %d failed, %d checks\n",
	       checks_total - checks_fail, checks_fail, checks_total);
	return checks_fail ? 1 : 0;
}
