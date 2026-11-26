// SPDX-License-Identifier: GPL-2.0
/*
 * dfs_set_selftest.c - host-side contract tests for hi5622_dfs_set.c
 * (lane wifi-5g-dfs-set, task-227).  Plain C99 + stdio, no kernel headers:
 * the driver file and this test share the exact same source.
 *
 * Build/run (tools/build-dfs-set.sh does this):
 *   gcc -std=c99 -Wall -Wextra -Werror -O2 -I<tree> -o dfs_set_selftest \
 *       <tree>/hi5622_dfs_set.c src/dfs_set_selftest.c
 */
#include "hi5622_dfs_set.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void check(const char *label, int ok)
{
	checks++;
	if (ok) {
		printf("PASS %3d  %s\n", checks, label);
	} else {
		failures++;
		printf("FAIL %3d  %s\n", checks, label);
	}
}

static void check_eq(const char *label, long got, long want)
{
	checks++;
	if (got == want) {
		printf("PASS %3d  %s\n", checks, label);
	} else {
		failures++;
		printf("FAIL %3d  %s (got %ld want %ld)\n",
		       checks, label, got, want);
	}
}

static struct hi5622_dfs_set_probe good_probe(void)
{
	struct hi5622_dfs_set_probe p;

	memset(&p, 0, sizeof(p));
	p.mctx = 0x00119758u;
	p.band = 1u;
	p.state = 1u;
	p.ec8 = 1u;
	p.cac = 0u;
	p.b4a0 = 0u;
	p.b4ac = 0x01010f18u;
	p.regs_pattern = 0x00u;
	return p;
}

int main(void)
{
	static const u8 arm_kat[HI5622_DFS_SET_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0xe7, 0x07, 0x01, 0x00,
		0x01, 0xff, 0xff, 0xff, 0xff
	};
	static const u8 clear_kat[HI5622_DFS_SET_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0xe7, 0x07, 0x01, 0x00,
		0x00, 0xff, 0xff, 0xff, 0xff
	};
	u8 wire[HI5622_DFS_SET_WIRE_LEN + 8];
	const u32 *regs;
	u16 regs16[HI5622_DFS_SET_REG_COUNT];
	size_t out_len = 0;
	struct hi5622_dfs_set_plan plan;
	struct hi5622_dfs_set_probe p;
	unsigned int i;
	char line[160];
	int rc;

	printf("=== plan (fail-closed parameter contract) ===\n");
	for (i = 0; i <= HI5622_DFS_SET_PARM_MAX; i++) {
		rc = hi5622_dfs_set_plan(i, 1u, 1u, &plan);
		check_eq("plan(valid mode on band 1) accepted", rc, 0);
		check_eq("plan post flag == (mode != 0)", plan.post, i != 0u);
		check_eq("plan mode echoes the parameter", plan.mode, (long)i);
	}
	check_eq("plan(mode 4) refused",
		 hi5622_dfs_set_plan(4u, 1u, 1u, &plan), -EINVAL);
	check_eq("plan(mode 1, no beacon_static) refused",
		 hi5622_dfs_set_plan(1u, 0u, 1u, &plan), -EINVAL);
	check_eq("plan(mode 0, no beacon_static) accepted",
		 hi5622_dfs_set_plan(0u, 0u, 1u, &plan), 0);
	check_eq("plan(mode 0, band 1) is a clean no-post",
		 plan.post, 0);
	check_eq("plan(mode 1, band 0) accepted",
		 hi5622_dfs_set_plan(1u, 1u, 0u, &plan), 0);
	check_eq("plan(mode 1, band 0) is a clean no-post", plan.post, 0);
	check_eq("plan(band 2) refused",
		 hi5622_dfs_set_plan(1u, 1u, 2u, &plan), -EINVAL);
	check_eq("plan(NULL) refused",
		 hi5622_dfs_set_plan(1u, 1u, 1u, NULL), -EINVAL);

	printf("\n=== frame builder (the 29-B g0-id1 carrier KAT) ===\n");
	memset(wire, 0xaa, sizeof(wire));
	rc = hi5622_dfs_set_build(wire, sizeof(wire), &out_len,
				  HI5622_DFS_SET_BODY_ARM);
	check_eq("arm build rc", rc, 0);
	check_eq("arm wire length", (long)out_len, HI5622_DFS_SET_WIRE_LEN);
	check("arm frame == the firmware-decode KAT",
	      memcmp(wire, arm_kat, sizeof(arm_kat)) == 0);
	check_eq("arm body byte", wire[HI5622_DFS_SET_BODY_OFF], 0x01);
	check_eq("arm key", (long)(wire[HI5622_DFS_SET_KEY_OFF] |
		 (wire[HI5622_DFS_SET_KEY_OFF + 1] << 8)), 0x07e7);
	check_eq("arm len", (long)wire[HI5622_DFS_SET_LEN_OFF], 1);
	check_eq("arm total field",
		 (long)(wire[4] | (wire[5] << 8)), HI5622_DFS_SET_WIRE_LEN);
	check_eq("arm tag", (long)(wire[10] | (wire[11] << 8)), 0x5a5a);
	check_eq("arm group nibble", wire[0] & 0x0f, 0);
	check_eq("arm id", (long)(wire[6] | (wire[7] << 8)), 1);
	check("arm 0xFF tail",
	      wire[25] == 0xff && wire[26] == 0xff &&
	      wire[27] == 0xff && wire[28] == 0xff);
	rc = hi5622_dfs_set_build(wire, sizeof(wire), &out_len,
				  HI5622_DFS_SET_BODY_CLEAR);
	check_eq("clear build rc", rc, 0);
	check("clear frame == the firmware-decode KAT",
	      memcmp(wire, clear_kat, sizeof(clear_kat)) == 0);
	check_eq("clear body byte", wire[HI5622_DFS_SET_BODY_OFF], 0x00);
	check("arm/clear differ only in the body byte",
	      memcmp(arm_kat, clear_kat, HI5622_DFS_SET_BODY_OFF) == 0 &&
	      memcmp(arm_kat + HI5622_DFS_SET_TAIL_OFF,
		     clear_kat + HI5622_DFS_SET_TAIL_OFF, 4) == 0);
	check_eq("build(body 2) refused",
		 hi5622_dfs_set_build(wire, sizeof(wire), &out_len, 2u),
		 -EINVAL);
	check_eq("build(NULL wire) refused",
		 hi5622_dfs_set_build(NULL, sizeof(wire), &out_len, 1u),
		 -EINVAL);
	check_eq("build(NULL out_len) refused",
		 hi5622_dfs_set_build(wire, sizeof(wire), NULL, 1u), -EINVAL);
	check_eq("build(-ENOSPC on a short buffer)",
		 hi5622_dfs_set_build(wire, HI5622_DFS_SET_WIRE_LEN - 1u,
				      &out_len, 1u), -ENOSPC);

	printf("\n=== register pattern helpers ===\n");
	regs = hi5622_dfs_set_regs();
	check("regs accessor non-NULL", regs != NULL);
	check_eq("reg 0 == 0x4010E000", (long)regs[0], 0x4010e000);
	check_eq("reg 1 == +4", (long)regs[1], 0x4010e004);
	check_eq("reg 2 == +8", (long)regs[2], 0x4010e008);
	check_eq("reg 3 == +0xA0", (long)regs[3], 0x4010e0a0);
	check_eq("reg 4 == +0xA4", (long)regs[4], 0x4010e0a4);
	check_eq("reg 5 == +0xA8", (long)regs[5], 0x4010e0a8);
	for (i = 0u; i < HI5622_DFS_SET_REG_COUNT; i++)
		regs16[i] = 0xfffeu;
	check_eq("pattern of an all-clear value == 0",
		 hi5622_dfs_set_reg_pattern(regs16), 0);
	check("an all-clear value is NOT armed",
	      !hi5622_dfs_set_armed(hi5622_dfs_set_reg_pattern(regs16)));
	/* the stock's armed state: {1,0,1,1,0,1} = 0x2D */
	for (i = 0u; i < HI5622_DFS_SET_REG_COUNT; i++)
		regs16[i] = (u16)(0xfffeu | (u16)((0x2d >> i) & 1u));
	check_eq("pattern of the armed state == 0x2D",
		 hi5622_dfs_set_reg_pattern(regs16), 0x2d);
	check_eq("ARM_MASK == 0x2D", (long)HI5622_DFS_SET_ARM_MASK, 0x2d);
	check("the armed state is armed",
	      hi5622_dfs_set_armed(hi5622_dfs_set_reg_pattern(regs16)));
	check_eq("pattern(NULL) == 0", hi5622_dfs_set_reg_pattern(NULL), 0);

	printf("\n=== precheck gates ===\n");
	p = good_probe();
	check_eq("good probe + ARM accepted",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), 0);
	check_eq("good probe + ARM_CLEAR accepted",
		 hi5622_dfs_set_precheck(&p,
			HI5622_DFS_SET_MODE_ARM_CLEAR), 0);
	check_eq("precheck(NULL) refused",
		 hi5622_dfs_set_precheck(NULL, HI5622_DFS_SET_MODE_ARM), -EINVAL);
	check_eq("precheck(mode 0) refused",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_OFF), -EINVAL);
	check_eq("precheck(mode 4) refused",
		 hi5622_dfs_set_precheck(&p, 4u), -EINVAL);
	p = good_probe();
	p.mctx = 0;
	check_eq("ARM refused without a module ctx",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -ENODEV);
	p = good_probe();
	p.b4ac = 0;
	check_eq("ARM refused without the helper object",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -ENODEV);
	p = good_probe();
	p.band = 0;
	check_eq("ARM refused on a non-5GHz band",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -EINVAL);
	p = good_probe();
	p.state = 0;
	check_eq("ARM refused before the VAP start (state 0)",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -EAGAIN);
	p = good_probe();
	p.state = 4;
	check_eq("ARM refused on an out-of-range state",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -EAGAIN);
	p = good_probe();
	p.ec8 = 0;
	check_eq("ARM refused when the band is not up",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -EAGAIN);
	p = good_probe();
	p.b4a0 = 1u;
	check_eq("ARM refused when u32[mctx+0x4A0] is set",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM), -EBUSY);
	p = good_probe();
	p.cac = 1u;
	check_eq("ARM refused when the CAC flag is already 1",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_ARM),
		 -EALREADY);
	p = good_probe();
	p.cac = 1u;
	p.regs_pattern = 0x2du;
	check_eq("good armed probe + CLEAR accepted",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_CLEAR), 0);
	p = good_probe();
	p.cac = 0u;
	p.regs_pattern = 0x2du;
	check_eq("CLEAR refused when the CAC flag is 0",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_CLEAR),
		 -EPERM);
	p = good_probe();
	p.cac = 1u;
	p.regs_pattern = 0x00u;
	check_eq("CLEAR refused when the registers are not armed",
		 hi5622_dfs_set_precheck(&p, HI5622_DFS_SET_MODE_CLEAR),
		 -EPERM);

	printf("\n=== verdict strings and postchecks ===\n");
	p = good_probe();
	check("ARM verdict says the gates are open",
	      strstr(hi5622_dfs_set_verdict(&p, HI5622_DFS_SET_MODE_ARM),
		     "ARM gates are open") != NULL);
	p = good_probe();
	p.state = 0;
	check("state-0 verdict names the 0x0065 requirement",
	      strstr(hi5622_dfs_set_verdict(&p, HI5622_DFS_SET_MODE_ARM),
		     "0x0065") != NULL);
	p = good_probe();
	p.b4ac = 0;
	check("missing-helper verdict names u32[mctx+0x4AC]",
	      strstr(hi5622_dfs_set_verdict(&p, HI5622_DFS_SET_MODE_ARM),
		     "0x4AC") != NULL);
	check("verdict(NULL) does not crash",
	      hi5622_dfs_set_verdict(NULL, HI5622_DFS_SET_MODE_ARM) != NULL);
	check_eq("ARM postcheck accepts flag 1",
		 hi5622_dfs_set_postcheck(1u, HI5622_DFS_SET_MODE_ARM), 0);
	check_eq("ARM postcheck rejects flag 0",
		 hi5622_dfs_set_postcheck(0u, HI5622_DFS_SET_MODE_ARM), -EIO);
	check_eq("CLEAR postcheck accepts flag 0",
		 hi5622_dfs_set_postcheck(0u, HI5622_DFS_SET_MODE_CLEAR), 0);
	check_eq("CLEAR postcheck rejects flag 1",
		 hi5622_dfs_set_postcheck(1u, HI5622_DFS_SET_MODE_CLEAR), -EIO);
	check_eq("ARM_CLEAR postcheck is the ARM check",
		 hi5622_dfs_set_postcheck(0u,
			HI5622_DFS_SET_MODE_ARM_CLEAR), -EIO);

	printf("\n=== status line ===\n");
	rc = hi5622_dfs_set_status_line(0u, -ENODATA, 0u, 0u, line,
					sizeof(line));
	check("status line returns a positive length", rc > 0);
	check("status line names 'off'", strstr(line, "(off)") != NULL);
	check("status line carries last_rc", strstr(line, "last_rc=") != NULL);
	check_eq("status line clamps to len-1",
		 hi5622_dfs_set_status_line(1u, 0, 1u, 0u, line, 8u), 7);
	check_eq("status line(NULL) refused",
		 hi5622_dfs_set_status_line(0u, 0, 0u, 0u, NULL, 8u), -EINVAL);
	rc = hi5622_dfs_set_status_line(2u, 0, 2u, 0u, line, sizeof(line));
	check("status line names 'arm+clear'",
	      rc > 0 && strstr(line, "(arm+clear)") != NULL);
	rc = hi5622_dfs_set_status_line(3u, 0, 1u, 0u, line, sizeof(line));
	check("status line names 'clear'",
	      rc > 0 && strstr(line, "(clear)") != NULL);

	printf("\nRESULT: %d/%d checks PASS\n",
	       checks - failures, checks);
	return failures == 0 ? 0 : 1;
}
