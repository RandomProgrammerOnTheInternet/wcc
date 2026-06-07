#include "ir.h"
#include "opt.h"
#include "aarch64.h"
#include "x64.h"

extern int debug;

/* optimize
 * %reg = leas #off
 * ...
 * %other_reg = load %reg
 * to
 * %other_reg = loads #off
 * and then
 * %reg = leas #off
 
 */
static int ir_stackopt(ir_func_t *func)
{
	int changed = 0;
	/* first, check which registers are leas */
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->type == IR_INST_LEAS) {
				ins->r0->stack_loc = true;
				ins->r0->stack_off = ins->imm;
			} else if(ins->r0) {
				ins->r0->stack_loc = false;
			}

			/* however if we use this register outside of loads and stores
			 * we can't optimize it */
			if(ins->r1 && ins->r1->stack_loc && ins->type != IR_INST_LOAD &&
			   ins->type != IR_INST_STORE) {
				ins->r1->stack_loc = false;
			}
			if(ins->r2 && ins->r2->stack_loc && ins->type != IR_INST_LOAD &&
			   ins->type != IR_INST_STORE) {
				ins->r2->stack_loc = false;
			}

			if(ins->type == IR_INST_CALL) {
				for(size_t i = 0; i < list_len(ins->call_args); i++) {
					reg_t *r = ins->call_args[i]->r;
					if(r && r->stack_loc && i >= 6) {
						r->stack_loc = false;
					}
				}
			}

			/* edge case */
			if(ins->r2 && ins->r2->stack_loc && ins->type == IR_INST_STORE) {
				ins->r2->stack_loc = false;
			}
		}
	}

	/* replace those leas with nops */
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->type == IR_INST_LEAS && ins->r0->stack_loc) {
				ins->type = IR_INST_NOP;
				changed = 1;
			}
		}
	}

	/* now, simply replace the load %leas_reg with loads #off */
	/* and store %leas_reg, %reg with stores %reg, #off */
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->type != IR_INST_LOAD && ins->type != IR_INST_STORE) {
				continue;
			}

			if(ins->type == IR_INST_LOAD && ins->r1->stack_loc) {
				ins->type = IR_INST_LOADS;
				ins->imm = -ins->r1->stack_off;
				changed = 1;
			}

			if(ins->type == IR_INST_STORE && ins->r1->stack_loc) {
				reg_t *r1 = ins->r1;
				ins->r1 = ins->r2;
				ins->type = IR_INST_STORES;
				ins->imm = -r1->stack_off;
				changed = 1;
			}
		}
	}

	return changed;
}

static int cmp_to_br(enum ins_type ins)
{
	if(!ir_inst_is_cmp(ins)) {
		return IR_INST_NOP;
	}
	switch(ins) {
	case IR_INST_EQ:
		return IR_INST_BREQ;
	case IR_INST_NE:
		return IR_INST_BRNE;
	case IR_INST_SLT:
		return IR_INST_BRSLT;
	case IR_INST_SLE:
		return IR_INST_BRSLE;
	case IR_INST_SGT:
		return IR_INST_BRSGT;
	case IR_INST_SGE:
		return IR_INST_BRSGE;
	case IR_INST_ULT:
		return IR_INST_BRULT;
	case IR_INST_ULE:
		return IR_INST_BRULE;
	case IR_INST_UGT:
		return IR_INST_BRUGT;
	case IR_INST_UGE:
		return IR_INST_BRUGE;
	default:
		return IR_INST_NOP;
	}
}

/* optimize
 * %cond = cmp.XX %r1, %r2
 * ...
 * br %cond, T, F
 * ->
 * br.XX %r1, %r2, T, F
 */
static int ir_branchopt(ir_func_t *func)
{
	int changed = 0;
	/* scan comparisons */
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ir_inst_is_cmp(ins->type)) {
				ins->r0->insty = ins->type;
				ins->r0->lhs = ins->r1;
				ins->r0->rhs = ins->r2;
				if(!ins->r2) {
					ins->r0->imm = ins->imm;
				}
				ins->r1->insty = ins->type;
				ins->r1->lhs = ins->r1;
				ins->r1->rhs = ins->r2;
				if(ins->r2) {
					ins->r2->insty = ins->type;
					ins->r2->lhs = ins->r1;
					ins->r2->rhs = ins->r2;
				}
				continue;
			}

			/* if written to discard this opt */
			if(ins->r0 && ir_inst_is_cmp(ins->r0->insty)) {
				ins->r0->insty = IR_INST_NOP;
				ins->r0->lhs = ins->r0->rhs = NULL;
				if(ins->r1) {
					ins->r1->insty = IR_INST_NOP;
					ins->r1->lhs = ins->r1->rhs = NULL;
				}
				if(ins->r2) {
					ins->r2->insty = IR_INST_NOP;
					ins->r2->lhs = ins->r2->rhs = NULL;
				}
				continue;
			}

			/* if read from any ins except `br` discard this opt */
			if(ins->r1 && ir_inst_is_cmp(ins->r1->insty) &&
			   ins->type != IR_INST_BR) {
				ins->r1->insty = IR_INST_NOP;
				ins->r1->lhs = ins->r1->rhs = NULL;
				continue;
			}

			if(ins->r2 && ir_inst_is_cmp(ins->r2->insty) &&
			   ins->type != IR_INST_BR) {
				ins->r2->insty = IR_INST_NOP;
				ins->r2->lhs = ins->r2->rhs = NULL;
				continue;
			}
		}
	}

	/* eliminate comparisons that have been merged with branches */

	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ir_inst_is_cmp(ins->type) && ins->r0->insty == ins->type &&
			   ins->r1->insty == ins->type) {
				if(ins->r2 && ins->r2->insty != ins->type) {
					goto false_pos;
				}
				ins->type = IR_INST_NOP;
				changed = 1;
				ins->r0 = ins->r1 = ins->r2 = NULL;
				continue;
			}
false_pos:

			if(ins->type == IR_INST_BR && ir_inst_is_cmp(ins->r1->insty)) {
				reg_t *cmp = ins->r1;
				ins->type = cmp_to_br(cmp->insty);
				ins->r1 = cmp->lhs;
				ins->r2 = cmp->rhs;
				ins->imm = cmp->imm;
				changed = 1;
			}
		}
	}
	return changed;
}

static int ir_simpleopt_ins(ir_inst_t *ins)
{
	int change = 0;
	/* %r0 = eor/sub/sdiv/udiv/smod/umod %r1, %r1
	 * ->
	 * %r0 = imm #0 */
	if((ins->type == IR_INST_EOR || ins->type == IR_INST_SUB ||
		ins->type == IR_INST_SDIV || ins->type == IR_INST_UDIV ||
		ins->type == IR_INST_SMOD || ins->type == IR_INST_UMOD) &&
	   ins->r1->vr == ins->r2->vr) {
		ins->type = IR_INST_IMM;
		ins->imm = 0;
		change = 1;
	}

	/* %r0 = and/or %r1, %r1
	 * ->
	 * %r0 = %r1
	 */
	if((ins->type == IR_INST_AND || ins->type == IR_INST_OR) &&
	   ins->r1->vr == ins->r2->vr) {
		ins->type = IR_INST_MOV;
		change = 1;
	}

	/* %r0 = sext.i64/zext.i64 %r1
	 * ->
	 * %r0 = %r1
	 */
	if((ins->type == IR_INST_SEXT || ins->type == IR_INST_ZEXT) &&
	   ins->size == 8) {
		ins->type = IR_INST_MOV;
		change = 1;
	}

	/* %r0 = %r0
	 * ->
	 * nop
	 */
	if(ins->type == IR_INST_MOV && ins->r0->vr == ins->r1->vr) {
		ins->type = IR_INST_NOP;
		change = 1;
	}

	/* %r0 = cmp.* %r1, %r1
	 * ->
	 * %r0 = imm #res */
	if(ir_inst_is_cmp(ins->type) && ins->r1->vr == ins->r2->vr) {
		long imm = 0;
#define CASE(v, x) \
	case v:        \
		imm = (x); \
		break
		switch(ins->type) {
			CASE(IR_INST_EQ, 1);
			CASE(IR_INST_NE, 0);
			CASE(IR_INST_SLT, 0);
			CASE(IR_INST_SLE, 1);
			CASE(IR_INST_SGT, 0);
			CASE(IR_INST_SGE, 1);
			CASE(IR_INST_ULT, 0);
			CASE(IR_INST_ULE, 1);
			CASE(IR_INST_UGT, 0);
			CASE(IR_INST_UGE, 1);
		default:
			break;
		}
#undef CASE
		ins->type = IR_INST_IMM;
		ins->imm = imm;
		change = 1;
	}

	/* br.** dontcareregs, blkA, blkA
	 * ->
	 * jmp blkA */
	if(ir_inst_is_br(ins->type) && ins->false_blk == ins->true_blk) {
		ins->type = IR_INST_JMP;
		change = 1;
	}

	return change;
}

/* trivial/simple optimizations */
static int ir_simpleopt(ir_func_t *func)
{
	int changed = 0;

	/* first, instruction-level opts */
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			changed |= ir_simpleopt_ins(ins);
		}
	}

	return changed;
}

/* changes
 * stores #imm, %r1
 * loads %r2, #imm
 * ->
 * stores #imm, %r1
 * %r2 = %r1
 */
static int ir_stackreduce(ir_func_t *func)
{
	int changed = 0;
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		ir_inst_t *nxt = blk->insts;
		for(ir_inst_t *ins = blk->insts; ins; ins = nxt) {
			nxt = ins->next;

			if(ins && nxt && ins->type == IR_INST_STORES &&
			   nxt->type == IR_INST_LOADS && ins->imm == nxt->imm &&
			   !ins->noopt && !nxt->noopt) {
				reg_t *r1 = ins->r1;
				reg_t *r2 = nxt->r0;
				changed = 1;
				/* bingo */
				nxt->type = IR_INST_MOV;
				nxt->r0 = r2;
				nxt->r1 = r1;
			}
		}
	}
	return changed;
}

/* optimizes an IR function */
void ir_opt(ir_func_t *func, int opt_level, enum ir_arch arch)
{
	int change = 0;
	int max_tolerated_change;
	switch(opt_level) {
	case 0:
		max_tolerated_change = 0;
		break;
	case 1:
		max_tolerated_change = 4;
		break;
	case 2:
		max_tolerated_change = 16;
		break;
	case 3:
		max_tolerated_change = 256; /* mimic the nature of -O3 */
		break;
	default:
		max_tolerated_change = 0;
		break;
	}

	if(debug) {
		printf("Before common opts:\n");
		ir_dump(func, 'v');
		printf("****\n");
	}

	int left = max_tolerated_change;
	while(left) {
		change = 0;

		/* trivial optimizations */
		{
			change |= ir_simpleopt(func);
			ir_nopremover(func);
			ir_fix(func);
		}

		/* stack-based optimizations */
		{
			change |= ir_stackopt(func);
			ir_nopremover(func);
			change |= ir_stackreduce(func);
			ir_nopremover(func);
			ir_fix(func);
		}

		/* branch opts */
		{
			change |= ir_branchopt(func);
			ir_nopremover(func);
			ir_fix(func);
		}

		if(!change) {
			break;
		}

		left--;
	}

	ir_nopremover(func);
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		list_hdr(func->blocks[i]->pred)->size = 0;
	}

	if(debug) {
		printf("After common opts:\n");
		ir_dump(func, 'v');
		printf("****\n");
	}

	/* apply arch specific opts */
	switch(arch) {
	case IR_ARCH_AARCH64_APPLE:
		ir_func_opt_aarch64(func, opt_level);
		break;
	case IR_ARCH_X64_SYSV:
		ir_func_opt_x64(func, opt_level);
		break;
	default:
		break;
	}

	ir_fix(func);

	if(debug) {
		printf("After arch opts:\n");
		ir_dump(func, 'v');
		printf("****\n");
	}
	return;
}
