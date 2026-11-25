/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rate_ctl_wire.h - the wire builder for the rate-control alg_cfg
 * config events (Hi5622V100 clean-room driver, wifi-rc-impl-2 lane, RC-2).
 *
 * The rate-control command carrier is the calibration ladder's bench-proven
 * HCC group 0 / id 2 / key 0x0101 config event: the 12-byte HCC header, the
 * 8-byte token, the u16 key, the u16 blob length, the alg_cfg blob and the
 * inherited 4-byte zero tail (28 bytes of carrier + the blob).  The 39-byte
 * SET rate_mode auto wire is derived in ../REPORT.md sec 3 and pinned byte
 * for byte by ../tools/rc2_frame_selftest.c.
 *
 * This unit is pure C (h2d_frames.h is the project's portable exact-frame
 * library) so the host selftest compiles it unchanged.  It constructs bytes
 * only: no device access, no register write, no libc beyond string.h.
 */
#ifndef HI5622_RATE_CTL_WIRE_H
#define HI5622_RATE_CTL_WIRE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif
#include "h2d_frames.h"
#include "hi5622_rate_ctl_logic.h"

/* 12-byte HCC header + 8 token + 2 key + 2 len + 4 tail. */
#define RC_WIRE_CARRIER_OVERHEAD	28u
/* The maximum wire this surface can build (10-byte alg header + 260 value). */
#define RC_WIRE_MAX_LEN	(RC_WIRE_CARRIER_OVERHEAD + RC_ALG_MAX_BLOB)

/* Build one complete alg_cfg config-event wire frame from an already-built
 * alg_cfg blob.  dst is the vendor's 0; fb is the module-context selector
 * (header field B; the proven calibration frames and the C2 type-1 create use
 * module 4).  Returns 0 on success, -EINVAL/-ENOSPC/-EPERM (fail closed) on a
 * malformed request; *out_len is the wire length (28 + blob_len). */
int rc_wire_build(const uint8_t *blob, uint16_t blob_len, uint8_t fb,
		  uint8_t *wire, size_t cap, size_t *out_len);

/* Convenience: build the blob from a protocol-level request (rc_blob_build's
 * strict validation included) and then the wire. */
int rc_wire_build_req(const struct rc_req *req, uint8_t fb,
		      uint8_t *wire, size_t cap, size_t *out_len);

#endif /* HI5622_RATE_CTL_WIRE_H */
