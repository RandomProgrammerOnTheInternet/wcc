#include "ssa.h"
#include "ir/ir.h"
#include "ir/regalloc.h"

static void find_before_last_term_ins(ir_blk_t *blk)
{
	ir_inst_t *prev = blk->insts;
	ir_inst_t *ret = prev->next;
	for(; ret; ret = ret->next) {
		if(ir_inst_is_term(ret->type)) {
			blk->tail = ret;
			blk->tailprev = prev;
			return;
		}
		prev = ret;
	}
	ASSERT(ir_inst_is_term(ret->type), "IR is not constructed properly");
}

void ir_ssa_enter(ir_func_t *fun)
{
	UNUSED(fun);
	ERROR("todo");
	return;
}

/* turn the IR from an SSA form into a typical 3AC IR.
 * Removes and deallocates all phis, turning them into NOPs. */
void ir_ssa_exit(ir_func_t *fun)
{
	/* append NOP to each block, find befores */
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ins_nop();
		nop->next = blk->insts;
		blk->insts = nop;
		find_before_last_term_ins(blk);
	}

	/* turn
	 * blk1: ...         blk2: ...
	 *		 jmp blk3         jmp blk3
	 * blk3: %r0 = phi [blk1, %r1], [blk2, %r2]
	 * ====into
	 * blk1: ...         blk2: ...
	 *       %r0 = %r1         %r0 = %r2
	 *       jmp blk3          jmp blk3
	 * blk3: nop
	*/

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		LIST(ir_blk_t *) preds = blk->pred;
		size_t preds_len = list_len(preds);
		for(ir_inst_t *inst = blk->insts; inst && inst->type == IR_INST_PHI;
			inst = inst->next) {
			LIST(reg_t *) args = inst->phi_args;
			reg_t *reg = inst->r0;
			ASSERT(list_len(args) == preds_len, "invalid SSA form");
			inst->type = IR_INST_NOP;

			for(size_t i = 0; i < preds_len; i++) {
				ir_inst_t *mov = ins_mov(reg, args[i]);
				mov->next = preds[i]->tail;
				preds[i]->tailprev->next = mov;
				preds[i]->tailprev = mov;
			}

			list_delete(args);
		}
	}
	return;
}
