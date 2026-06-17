#include "bird/bird.h"
#include "bird.h"
#include "bird/ir.h"
#include "ir.h"

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

static void print_list(LIST(long) list)
{
	for(size_t i = 0; i < list_len(list); i++) {
		printf("%ld, ", list[i]);
	}
	printf("\n");
	return;
}

static bool has_long(LIST(long) list, long want)
{
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i] == want) {
			return true;
		}
	}
	return false;
}

static bool has_blk(LIST(ir_blk_t *) list, ir_blk_t *want)
{
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i] == want) {
			return true;
		}
	}
	return false;
}

static bool has_reg(LIST(reg_t *) list, reg_t *want)
{
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i] == want) {
			return true;
		}
	}
	return false;
}

static bool has_blkreg(LIST(blkreg_t *) list, ir_blk_t *blk)
{
	if(!list) {
		return false;
	}
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i]->blk == blk) {
			return true;
		}
	}
	return false;
}

static size_t find_blkreg(LIST(blkreg_t *) list, ir_blk_t *blk)
{
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i]->blk == blk) {
			return i;
		}
	}
	return -1;
}

/* I'm using A Simple, Fast Dominance Algorithm by Keith D. Cooper, Timothy J. Harvey, and Ken Kennedy. */
/* The postorder computation was inspired from (here)[https://github.com/sampsyo/bril/blob/main/examples/dom.py#L34]. */
/* The computation was also inspired from (here)[https://github.com/Golf0ned/PANNICC/blob/main/src/middleend/analysis/dominator_tree.cpp]. */

static void union_(LIST(long) l, long x)
{
	if(!has_long(l, x)) {
		list_append(l, x);
	}
}

static void postorder_visit(LIST(long) postorder, ir_blk_t *blk)
{
	if(blk->visited) {
		return;
	}

	size_t len = list_len(postorder);

	blk->visited = true;
	union_(postorder, blk->num);

	if(blk->tail->true_blk) {
		union_(postorder, blk->tail->true_blk->num);
		postorder_visit(postorder, blk->tail->true_blk);
	}
	if(blk->tail->false_blk) {
		union_(postorder, blk->tail->false_blk->num);
		postorder_visit(postorder, blk->tail->false_blk);
	}

	return;
}

static LIST(long) postorder_get(LIST(ir_blk_t *) blocks)
{
	LIST(long) postorder = list_make(long);
	/* clear if blocks are visited */
	for(size_t i = 0; i < list_len(blocks); i++) {
		blocks[i]->visited = false;
	}

	postorder_visit(postorder, blocks[0]);

	/* clear if blocks are visited */
	for(size_t i = 0; i < list_len(blocks); i++) {
		blocks[i]->visited = false;
	}
	return postorder;
}

static LIST(long) reverse_postorder(LIST(long) postorder)
{
	LIST(long) rev_postorder = list_make(long);

	for(size_t i = list_len(postorder) - 1; i >= 0; i--) {
		list_append(rev_postorder, postorder[i]);
		if(i == 0) {
			break;
		}
	}

	return rev_postorder;
}

static bool block_contains_regdef(ir_blk_t *blk, reg_t *reg)
{
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		if(ins->r0 && ins->r0->vr == reg->vr) {
			return true;
		}
	}
	return false;
}

static blkreg_t *blkreg_make(ir_blk_t *blk, reg_t *reg)
{
	blkreg_t *r = zalloc(sizeof(blkreg_t));
	r->blk = blk;
	r->reg = reg;

	return r;
}

static reg_t *ssa_tmp(reg_t *var)
{
	reg_t *r = reg_make();
	r->ssareg = var;
	return r;
}

static reg_t *find_var(reg_t *reg)
{
	return reg->ssareg ? find_var(reg->ssareg) : reg;
}

static reg_t *read_reg_lvn(ir_blk_t *blk, reg_t *reg)
{
	reg_t *var = find_var(reg);
	if(has_blkreg(var->blkregs, blk)) {
		return var->blkregs[find_blkreg(var->blkregs, blk)]->reg;
	}

	/* can't do right now */
	return var;
}

static ir_func_t *func;
static LIST(ir_blk_t *) sealed_blks;

static reg_t *write_reg(ir_blk_t *blk, reg_t *reg, reg_t *val)
{
	reg_t *var = find_var(reg);

	if(!has_blkreg(var->blkregs, blk)) {
		list_append(var->blkregs, blkreg_make(blk, val));
	} else {
		var->blkregs[find_blkreg(var->blkregs, blk)]->reg = val;
	}

	return val;
}

static ir_inst_t *insert_phi(ir_blk_t *blk)
{
	ir_inst_t *phi = ir_inst_make(IR_INST_PHI, NULL, NULL, NULL, 0);
	phi->phi_args = list_make(reg_t *);
	phi->next = blk->insts->next;
	blk->insts->next = phi;
	return phi;
}

static reg_t *read_reg_gvn(ir_blk_t *blk, reg_t *reg);

static reg_t *add_phi_ops(ir_blk_t *blk, reg_t *var, ir_inst_t *phi)
{
	for(size_t i = 0; i < list_len(blk->pred); i++) {
		list_append(phi->phi_args, read_reg_gvn(blk->pred[i], var));
	}
	return phi->r0;
}

static reg_t *read_var_rec(ir_blk_t *blk, reg_t *reg)
{
	reg_t *val;
	ir_inst_t *phi = NULL;
	if(!has_blk(sealed_blks, blk)) {
		phi = insert_phi(blk);
		val = phi->r0 = ssa_tmp(reg);
		list_append(blk->incomplete_phis, phi);
	} else if(list_len(blk->pred) == 1) {
		val = read_reg_gvn(blk->pred[0], reg);
	} else {
		/* insert placeholder phi */
		phi = insert_phi(blk);
		phi->r0 = write_reg(blk, reg, ssa_tmp(reg));
		val = add_phi_ops(blk, reg, phi);
	}
	write_reg(blk, reg, val);
	return val;
}

static void breakpoint(reg_t *var, ir_blk_t *blk, reg_t *reg)
{
	return;
}

static reg_t *read_reg_gvn(ir_blk_t *blk, reg_t *reg)
{
	reg_t *var = find_var(reg);
	if(!var->blkregs) {
		breakpoint(var, blk, reg);
	}

	if(has_blkreg(var->blkregs, blk)) {
		return var->blkregs[find_blkreg(var->blkregs, blk)]->reg;
		// return reg;
	}

	return read_var_rec(blk, reg);
}

static void seal_block(ir_blk_t *blk)
{
	if(has_blk(sealed_blks, blk)) {
		return;
	}

	for(size_t i = 0; i < list_len(blk->incomplete_phis); i++) {
		ir_inst_t *phi = blk->incomplete_phis[i];
		add_phi_ops(blk, phi->r0, phi);
	}
	list_append(sealed_blks, blk);
}

void ir_ssa_enter(ir_func_t *fun)
{
	reg_reset_counter();
	UNUSED(fun);
	ir_fix(fun);
	ir_nopremover(fun);
	ir_blk_flow(fun);
	func = fun;

	sealed_blks = list_make(ir_blk_t *);

	/* append NOP to each block, find befores */
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ins_nop();
		nop->next = blk->insts;
		blk->insts = nop;
		find_before_last_term_ins(blk);
	}

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->r0 && !inst->r0->blkregs) {
				inst->r0->blkregs = list_make(blkreg_t *);
			}
		}
	}

	LIST(long) postorder = postorder_get(fun->blocks);
	LIST(long) rev_postorder = reverse_postorder(postorder);
	// print_list(postorder);

	/* local value numbering */

	for(size_t ip = 0; ip < list_len(postorder); ip++) {
		size_t i = postorder[ip];
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->r0) {
				inst->r0 = write_reg(blk, inst->r0, ssa_tmp(inst->r0));
			}
			if(inst->r1) {
				inst->r1 = read_reg_gvn(blk, inst->r1);
			}
			if(inst->r2) {
				inst->r2 = read_reg_gvn(blk, inst->r2);
			}
			if(inst->type == IR_INST_CALL) {
				for(size_t i = 0; i < list_len(inst->call_args); i++) {
					inst->call_args[i]->r =
						read_reg_gvn(blk, inst->call_args[i]->r);
				}
			}
		}
	}

	ir_nopremover(fun);
	// ir_dump(fun, 'v');
	// exit(1);

	for(size_t i = 0; i < list_len(postorder); i++) {
		// printf("******\n\n\n\n\n\n");
		// ir_dump(fun, 'v');
		seal_block(fun->blocks[i]);
	}

	/* global value numbering */
	/*
	for(size_t ip = 0; ip < list_len(postorder); ip++) {
		size_t i = postorder[ip];
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->r1) {
				inst->r1 = read_reg_gvn_normal(blk, inst->r1);
			}
			if(inst->r2) {
				inst->r2 = read_reg_gvn_normal(blk, inst->r2);
			}
			if(inst->type == IR_INST_CALL) {
				for(size_t i = 0; i < list_len(inst->call_args); i++) {
					inst->call_args[i]->r =
						read_reg_gvn_normal(blk, inst->call_args[i]->r);
				}
			}
		}
	}
	*/

	list_delete(sealed_blks);
	list_delete(postorder);
	list_delete(rev_postorder);

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
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->type != IR_INST_PHI) {
				continue;
			}
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
