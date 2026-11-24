/* request_dump.c - regenerate the exact g0 id0xC wire bytes of one 13-channel
 * 2.4 GHz scan request (wifi-scan-tuning lane, task-133). Host-only. */
#include <stdio.h>
#include <string.h>
#include "vapscan_frames.h"

int main(void)
{
	struct vs_scan_params p;
	struct vs_frame f;
	u8 i;
	int rc;

	vs_scan_defaults(&p);
	p.chip_id = 0;
	p.vap_id = 0;
	p.dst = 0;
	p.n_ssids = 1;			/* wildcard SSID, iw scan */
	p.ssids[0].len = 0;
	p.n_chans = 13;
	for (i = 0; i < 13; i++) {
		p.chans[i].num = (u8)(i + 1);
		p.chans[i].band = 0;
		p.chans[i].pad = 0;
		p.chans[i].idx = i;	/* vendor band channel index [I] */
	}
	rc = vs_build_scan_req(&f, &p);
	if (rc) { printf("build failed rc=%d\n", rc); return 1; }
	printf("wire_total=%u payload=%u\n", f.len, (unsigned)(f.len - VS_HDR_LEN));
	printf("hdr: 00 01 00 00 18 00 0c 00 00 00 5a 5a (family byte[6]=0x0c=sr data)\n");
	for (i = 0; i < f.len; i++) {
		printf("%02x ", f.buf[i]);
		if ((i & 15) == 15 || i + 1 == f.len) printf("\n");
	}
	printf("payload+0x000 flags0     = 0x%02x\n", f.buf[VS_HDR_LEN + 0x00]);
	printf("payload+0x001 flags1     = 0x%02x\n", f.buf[VS_HDR_LEN + 0x01]);
	printf("payload+0x002 has_bssid  = 0x%02x\n", f.buf[VS_HDR_LEN + 0x02]);
	printf("payload+0x003 n_ssids    = 0x%02x\n", f.buf[VS_HDR_LEN + 0x03]);
	printf("payload+0x00b type_b     = 0x%02x\n", f.buf[VS_HDR_LEN + 0x0b]);
	printf("payload+0x0c bssid       = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       f.buf[VS_HDR_LEN + 0x0c], f.buf[VS_HDR_LEN + 0x0d],
	       f.buf[VS_HDR_LEN + 0x0e], f.buf[VS_HDR_LEN + 0x0f],
	       f.buf[VS_HDR_LEN + 0x10], f.buf[VS_HDR_LEN + 0x11]);
	printf("payload+0x131 flag       = 0x%02x\n", f.buf[VS_HDR_LEN + 0x131]);
	printf("payload+0x138 count2     = 0x%02x\n", f.buf[VS_HDR_LEN + 0x138]);
	printf("payload+0x13b vapflag    = 0x%02x\n", f.buf[VS_HDR_LEN + 0x13b]);
	printf("payload+0x13c timeoutmode= 0x%02x\n", f.buf[VS_HDR_LEN + 0x13c]);
	for (i = 0; i < 13; i++) {
		const u8 *e = &f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_AREA + (u16)i * 4];

		printf("payload+0x%03x chan[%2u]  = {num=%u band=%u pad=%u idx=%u}\n",
		       VS_SC_OFF_CHAN_AREA + i * 4, i, e[0], e[1], e[2], e[3]);
	}
	printf("payload+0x1ec count      = 0x%02x (%u)\n",
	       f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_COUNT],
	       f.buf[VS_HDR_LEN + VS_SC_OFF_CHAN_COUNT]);
	printf("payload+0x1ed            = 0x%02x\n", f.buf[VS_HDR_LEN + 0x1ed]);
	printf("payload+0x1ee dwell u16  = 0x%04x (%u)\n",
	       (unsigned)(f.buf[VS_HDR_LEN + 0x1ee] |
			  (f.buf[VS_HDR_LEN + 0x1ef] << 8)),
	       (unsigned)(f.buf[VS_HDR_LEN + 0x1ee] |
			  (f.buf[VS_HDR_LEN + 0x1ef] << 8)));
	printf("payload+0x1f0 scan_type  = 0x%02x\n", f.buf[VS_HDR_LEN + 0x1f0]);
	printf("payload+0x1f1 one        = 0x%02x\n", f.buf[VS_HDR_LEN + 0x1f1]);
	printf("payload+0x1f2 type11     = 0x%02x\n", f.buf[VS_HDR_LEN + 0x1f2]);
	printf("payload+0x1f8 tail u32   = 0x%02x%02x%02x%02x\n",
	       f.buf[VS_HDR_LEN + 0x1fb], f.buf[VS_HDR_LEN + 0x1fa],
	       f.buf[VS_HDR_LEN + 0x1f9], f.buf[VS_HDR_LEN + 0x1f8]);
	return 0;
}

