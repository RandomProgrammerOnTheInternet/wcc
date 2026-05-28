#include "ir.h"
#include "ir_opt.h"
#include "ir_aarch64.h"
#include "ir_x64.h"

extern int debug;

/* is this instruction foldable? */
static int ir_inst_is_foldable(enum ins_type type)
{
	return ir_inst_is_cmp(type) || type == IR_INST_ADD || type == IR_INST_SUB ||
		   type == IR_INST_MUL || type == IR_INST_DIV || type == IR_INST_ADDI ||
		   type == IR_INST_SUBI || type == IR_INST_MULI || type == IR_INST_DIVI;
}

static int promote_assoc_to_assoc_imm(enum ins_type type)
{
	if(!ir_inst_is_assoc(type)) {
		return type;
	}
	switch(type) {
	case IR_INST_ADD:
		return IR_INST_ADDI;
	case IR_INST_MUL:
		return IR_INST_MULI;
	case IR_INST_EQ:
		return IR_INST_EQI;
	case IR_INST_NE:
		return IR_INST_NEI;
	case IR_INST_LT:
		return IR_INST_LTI;
	case IR_INST_LE:
		return IR_INST_LEI;
	case IR_INST_GT:
		return IR_INST_GTI;
	case IR_INST_GE:
		return IR_INST_GEI;
	default:
		return IR_INST_NOP;
	}
	return IR_INST_NOP;
}

/* move elimination */
static int ir_mov_elim(ir_func_t *func)
{
	int changed = 0;
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->type == IR_INST_MOV) {
				changed = 1;
				ins->r0->insty = IR_INST_MOV;
				ins->r0->lhs = ins->r1;
				ins->type = IR_INST_NOP;
				continue;
			}

			/* if written to stop the elim */
			if(ins->r0 && ins->r0->insty == IR_INST_MOV) {
				ins->r0->insty = IR_INST_NOP;
				continue;
			}

			if(ins->r1 && ins->r1->insty == IR_INST_MOV) {
				changed = 1;
				ins->r1 = ins->r1->lhs;
			}

			if(ins->r2 && ins->r2->insty == IR_INST_MOV) {
				changed = 1;
				ins->r2 = ins->r2->lhs;
			}

			if(ins->type == IR_INST_CALL) {
				for(size_t i = 0; i < list_len(ins->call_args); i++) {
					reg_t *r = ins->call_args[i];
					if(r->insty == IR_INST_MOV) {
						changed = 1;
						ins->call_args[i] = ins->call_args[i]->lhs;
					}
				}
			}
		}
	}
	return changed;
}

/* constant folding */
static int ir_fold(ir_func_t *func)
{
	int changed = 0;
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->type == IR_INST_IMM) {
				ins->r1 = NULL;
set_imm:
				ins->r0->imm = ins->imm;
				ins->r0->insty = IR_INST_IMM;
				continue;
			}

			/* if another value stored to this instruction, remove it's immediate status */
			if(ins->r0 && ins->r0->insty == IR_INST_IMM) {
				changed = 1;
				ins->r0->imm = 0;
				ins->r0->insty = IR_INST_NOP;
				continue;
			}

			/* fold */
			if(ins->type == IR_INST_MOV && ins->r1->insty == IR_INST_IMM) {
				changed = 1;
				ins->type = IR_INST_IMM;
				ins->imm = ins->r1->imm;
				ins->r1 = NULL;
				goto set_imm;
				continue;
			}

			if(ins->type == IR_INST_NEG && ins->r1->insty == IR_INST_IMM) {
				changed = 1;
				ins->type = IR_INST_IMM;
				ins->imm = -(long)ins->r1->imm;
				ins->r1 = NULL;
				goto set_imm;
				continue;
			}

			/* fold 3-sources with both imms */
			if(ir_inst_is_foldable(ins->type) && ins->r1 && ins->r2 &&
			   ins->r1->insty == IR_INST_IMM && ins->r2->insty == IR_INST_IMM) {
				changed = 1;
				int oldtype = ins->type;
				ins->type = IR_INST_IMM;
				long a1 = ins->r1->imm;
				long a2 = ins->r2->imm;
				ins->r1 = NULL;
				ins->r2 = NULL;
				switch(oldtype) {
				case IR_INST_ADD:
					ins->imm = a1 + a2;
					break;
				case IR_INST_SUB:
					ins->imm = a1 - a2;
					break;
				case IR_INST_MUL:
					ins->imm = a1 * a2;
					break;
				case IR_INST_DIV:
					/* define division by zero to be zero */
					if(a2 == 0) {
						a1 = 0;
						a2 = 1;
					}
					ins->imm = a1 / a2;
					break;
				case IR_INST_EQ:
					ins->imm = a1 == a2;
					break;
				case IR_INST_NE:
					ins->imm = a1 != a2;
					break;
				case IR_INST_LT:
					ins->imm = a1 < a2;
					break;
				case IR_INST_LE:
					ins->imm = a1 <= a2;
					break;
				case IR_INST_GT:
					ins->imm = a1 > a2;
					break;
				case IR_INST_GE:
					ins->imm = a1 >= a2;
					break;
				default:
					break;
				}
				goto set_imm;
				continue;
			}

			/* fold assocs with only 1 imm */
			if(ir_inst_is_foldable(ins->type) && ir_inst_is_assoc(ins->type) &&
			   ins->r1 && ins->r2 &&
			   (ins->r1->insty == IR_INST_IMM ||
				ins->r2->insty == IR_INST_IMM)) {
				changed = 1;
				/* reorder so that imm is on r2 */
				if(ins->r1->insty == IR_INST_IMM) {
					reg_t *swap = ins->r1;
					ins->r1 = ins->r2;
					ins->r2 = swap;
					/* if comparison, change to inverse cond */
					if(ins->type == IR_INST_LT || ins->type == IR_INST_LE ||
					   ins->type == IR_INST_GT || ins->type == IR_INST_GE) {
						switch(ins->type) {
						case IR_INST_LT:
							ins->type = IR_INST_GT;
							break;
						case IR_INST_LE:
							ins->type = IR_INST_GE;
							break;
						case IR_INST_GT:
							ins->type = IR_INST_LT;
							break;
						case IR_INST_GE:
							ins->type = IR_INST_LE;
							break;
						default:
							break;
						}
					}
				}

				ins->type = promote_assoc_to_assoc_imm(ins->type);
				ins->imm = ins->r2->imm;
				continue;
			}

			/* fold the half-folded ins */
			if(ir_inst_r2_imm(ins->type) && ins->r1->insty == IR_INST_IMM) {
				changed = 1;
				long res = 0;
				long a1 = ins->r1->imm;
				long a2 = ins->imm;
				ins->type = IR_INST_IMM;
				switch(ins->type) {
				case IR_INST_ADDI:
					res = a1 + a2;
					break;
				case IR_INST_SUBI:
					res = a1 - a2;
					break;
				case IR_INST_MULI:
					res = a1 * a2;
					break;
				case IR_INST_DIVI:
					res = a1 / a2;
					break;
				case IR_INST_EQI:
					res = a1 == a2;
					break;
				case IR_INST_NEI:
					res = a1 != a2;
					break;
				case IR_INST_LTI:
					res = a1 < a2;
					break;
				case IR_INST_LEI:
					res = a1 <= a2;
					break;
				case IR_INST_GTI:
					res = a1 > a2;
					break;
				case IR_INST_GEI:
					res = a1 >= a2;
					break;

				default:
					break;
				case IR_INST_BREQI:
					ins->type = IR_INST_JMP;
					res = a1 == a2;
					break;
				case IR_INST_BRNEI:
					ins->type = IR_INST_JMP;
					res = a1 != a2;
					break;
				case IR_INST_BRLTI:
					ins->type = IR_INST_JMP;
					res = a1 < a2;
					break;
				case IR_INST_BRLEI:
					ins->type = IR_INST_JMP;
					res = a1 <= a2;
					break;
				case IR_INST_BRGTI:
					ins->type = IR_INST_JMP;
					res = a1 > a2;
					break;
				case IR_INST_BRGEI:
					ins->type = IR_INST_JMP;
					res = a1 >= a2;
					break;
				}

				if(ins->type == IR_INST_IMM) {
					ins->imm = res;
					continue;
				}

				if(!res && ins->type == IR_INST_JMP) {
					ins->true_blk = ins->false_blk;
				}

				continue;
			}
		}
	}
	return changed;
}

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
					reg_t *r = ins->call_args[i];
					if(r && r->stack_loc) {
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
	case IR_INST_LT:
		return IR_INST_BRLT;
	case IR_INST_LE:
		return IR_INST_BRLE;
	case IR_INST_GT:
		return IR_INST_BRGT;
	case IR_INST_GE:
		return IR_INST_BRGE;
	case IR_INST_EQI:
		return IR_INST_BREQI;
	case IR_INST_NEI:
		return IR_INST_BRNEI;
	case IR_INST_LTI:
		return IR_INST_BRLTI;
	case IR_INST_LEI:
		return IR_INST_BRLEI;
	case IR_INST_GTI:
		return IR_INST_BRGTI;
	case IR_INST_GEI:
		return IR_INST_BRGEI;
	default:
		return IR_INST_NOP;
	}
}

static int ir_optzero(ir_func_t *func)
{
	int changed = 0;
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->imm) {
				continue;
			}
			switch(ins->type) {
			case IR_INST_ADDI:
			case IR_INST_SUBI:
				/* %r0 = addi/subi %r1, 0 ->
					 * %r0 = %r1 */
				ins->type = IR_INST_MOV;
				changed = 1;
				break;
			case IR_INST_MULI:
			case IR_INST_DIVI:
				/* %r0 = muli/divi %r1, 0 ->
					 * %r0 = #0 */
				ins->type = IR_INST_IMM;
				changed = 1;
				break;
			default:
				break;
			}
		}
	}
	return changed;
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
	/* scan comparisions */
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

	/* eliminate comparisions that have been merged with branches */

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
			   nxt->type == IR_INST_LOADS && ins->imm == nxt->imm) {
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
		max_tolerated_change = 2;
		break;
	case 2:
		max_tolerated_change = 4;
		break;
	case 3:
		max_tolerated_change = 8;
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
		/* stack-based optimizations */
		{
			change |= ir_stackopt(func);
			ir_nopremover(func);
			change |= ir_stackreduce(func);
			change |= ir_mov_elim(func);
			ir_nopremover(func);
			ir_fix(func);
		}

		/* branch opts */
		{
			change |= ir_branchopt(func);
			ir_nopremover(func);
			ir_fix(func);
		}

		/* fold opts */
		{
			change |= ir_fold(func);
			change |= ir_optzero(func);
			ir_fix(func);
			change |= ir_mov_elim(func);
			ir_nopremover(func);
			ir_fix(func);
		}

		if(!change) {
			break;
		}

		left--;
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
