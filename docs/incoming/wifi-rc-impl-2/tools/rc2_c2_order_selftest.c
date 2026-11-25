// SPDX-License-Identifier: GPL-2.0
/*
 * rc2_c2_order_selftest.c - host test of the CORRECTED C2 ORDER (RC-2).
 *
 * Exercises the PATCHED portable gate code on the current base tree - not a
 * model of it:
 *   - h2d_gate_check() (h2d_send_logic.c): the ordered ladder must refuse a
 *     TXQ frame unless this load's context-create was TYPE 1 and its
 *     ctx4+0x190 attestation held (fw-h2d-stall task-181; the earlier RC fire
 *     wrote type 3 and the announce stalled on the resulting fault);
 *   - rc_precond_check() (hi5622_rate_ctl_logic.c): the rate_ctl surface
 *     refuses every command until the same three facts were proven.
 *
 * The attestation itself is a read-only firmware-RAM read in the kernel glue
 * (h2d_send.c); here its RESULT is the h2d_live.ctx_attested input, exactly as
 * the kernel passes it.  No device, no kernel tree.
 *
 * Build (see tools/run-host-tests.sh):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -I<tree> \
 *      rc2_c2_order_selftest.c h2d_send_logic.c h2d_frames.c \
 *      hi5622_rate_ctl_logic.c
 */
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"
#include "h2d_send_logic.h"
#include "hi5622_rate_ctl_logic.h"

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

/* A live snapshot of a healthy boot that reached the ordered TXQ phase. */
static struct h2d_live base_live(void)
{
	struct h2d_live live;

	memset(&live, 0, sizeof(live));
	live.armed = true;
	live.released = true;
	live.boot_latched = true;
	live.phase = HCC_PHASE_READY_TXQ_SYNC;
	live.budget = 8;
	live.frames_sent = 1;		/* the context-create frame */
	live.ctx_created = true;
	live.ctx_create_type = 1;
	live.ctx_attested = true;
	return live;
}

int main(void)
{
	struct h2d_live live;

	/* ======= 1. the corrected C2 gate order (the real patched gate) ===== */
	live = base_live();
	check("ladder: type-1 + attested + READY -> TXQ12 allowed",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_OK);
	check("ladder: same state -> TXQ allowed",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC) == H2D_GATE_OK);

	live = base_live();
	live.ctx_created = false;
	live.ctx_create_type = 0;
	live.ctx_attested = false;
	check("ladder: no context-create -> TXQ refused (NEED_CTX)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_NEED_CTX);

	live = base_live();
	live.ctx_create_type = 3;	/* the old RC fire's create */
	check("ladder: TYPE 3 create -> TXQ refused (CTX_TYPE)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_CTX_TYPE);
	live.ctx_create_type = 2;
	check("ladder: TYPE 2 create -> TXQ refused (CTX_TYPE)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_CTX_TYPE);

	live = base_live();
	live.ctx_attested = false;	/* ctx4+0x190 still 0 */
	check("ladder: no ctx190 attestation -> TXQ refused (NEED_CTX_ATTEST)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) ==
	      H2D_GATE_NEED_CTX_ATTEST);

	live = base_live();
	live.phase = HCC_PHASE_ACTIVE;
	check("ladder: ACTIVE after create -> TXQ12 allowed once",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_OK);
	live.txq_submitted = true;
	check("ladder: a second TXQ refused (DUP_TXQ)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_DUP_TXQ);

	live = base_live();
	live.budget = 1;
	check("ladder: budget exhausted -> TXQ refused (BUDGET)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) == H2D_GATE_BUDGET);

	live = base_live();
	live.out0 = 0x8;
	check("ladder: previous announce unconsumed -> TXQ refused (OUT0)",
	      h2d_gate_check(&live, H2D_MSG_TXQ_SYNC72) ==
	      H2D_GATE_OUT0_PENDING);

	/* context-create itself: READY-only, once per load */
	live = base_live();
	live.ctx_created = false;
	live.frames_sent = 0;
	live.ctx_create_type = 0;
	live.ctx_attested = false;
	check("ctx_create allowed in READY", h2d_gate_check(&live,
	      H2D_MSG_CTX_CREATE_T1) == H2D_GATE_OK);
	live.phase = HCC_PHASE_ACTIVE;
	check("ctx_create refused in ACTIVE (CTX_PHASE)",
	      h2d_gate_check(&live, H2D_MSG_CTX_CREATE_T1) ==
	      H2D_GATE_CTX_PHASE);
	live = base_live();
	check("ctx_create refused after a create (DUP_CTX)",
	      h2d_gate_check(&live, H2D_MSG_CTX_CREATE_T1) == H2D_GATE_DUP_CTX);

	/* ======= 2. the rate_ctl precondition (the real patched matrix) ===== */
	{
		struct rc_policy pol;

		memset(&pol, 0, sizeof(pol));
		pol.enabled = true;
		pol.budget = 8;
		check("rc: no type-1 create -> NO_CTX_TYPE1",
		      rc_precond_check(&pol) == RC_DEC_NO_CTX_TYPE1);
		pol.ctx_type1 = true;
		check("rc: type-1 but no attestation -> NO_CTX_ATTEST",
		      rc_precond_check(&pol) == RC_DEC_NO_CTX_ATTEST);
		pol.ctx_attested = true;
		check("rc: attested but no ordered TXQ -> NO_TXQ_SYNC",
		      rc_precond_check(&pol) == RC_DEC_NO_TXQ_SYNC);
		pol.txq_sent = true;
		check("rc: full ladder -> precond OK",
		      rc_precond_check(&pol) == RC_DEC_ALLOW);

		/* the SET rate_mode auto goes through only with the ladder */
		{
			struct rc_req req;

			check("rc: SET auto builder",
			      rc_req_rate_mode(&req, RC_RATE_MODE_AUTO) == 0);
			check("rc: SET auto allowed with the proven ladder",
			      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) ==
			      RC_DEC_ALLOW);
			pol.ctx_type1 = false;
			check("rc: SET auto refused on the type-3 state",
			      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) ==
			      RC_DEC_NO_CTX_TYPE1);
			pol.ctx_type1 = true;
			pol.ctx_attested = false;
			check("rc: SET auto refused without ctx190 attestation",
			      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) ==
			      RC_DEC_NO_CTX_ATTEST);
			pol.ctx_attested = true;
			pol.txq_sent = false;
			check("rc: SET auto refused before the ordered TXQ",
			      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) ==
			      RC_DEC_NO_TXQ_SYNC);
			pol.txq_sent = true;
			pol.enabled = false;
			check("rc: disabled still wins over the precond",
			      rc_check(&pol, &req, RC_OP_RATE_MODE_SET) ==
			      RC_DEC_DISABLED);
		}
	}

	/* ======= 3. gate/vs-precond consistency ======================= */
	{
		/* The precond facts are exactly the gate's inputs: a state where
		 * the H2D gate allows the TXQ must never let the RC precond
		 * refuse, and vice versa. */
		struct rc_policy pol;
		struct h2d_live l;

		memset(&pol, 0, sizeof(pol));
		pol.enabled = true;
		pol.budget = 8;
		l = base_live();
		pol.ctx_type1 = l.ctx_create_type == 1u;
		pol.ctx_attested = l.ctx_attested;
		pol.txq_sent = false;
		check("consistency: TXQ gate OK while RC still awaits the TXQ",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC72) == H2D_GATE_OK &&
		      rc_precond_check(&pol) == RC_DEC_NO_TXQ_SYNC);
		pol.txq_sent = true;
		check("consistency: after the TXQ both allow",
		      rc_precond_check(&pol) == RC_DEC_ALLOW);
	}

	printf("rc2_c2_order_selftest: %d passed, %d failed, %d checks\n",
	       checks_total - checks_fail, checks_fail, checks_total);
	return checks_fail ? 1 : 0;
}
