/* Implementation of the "Simple and Efficient Construction of Static Single Assignment Form" algorithm, by Matthias Braun, Sebastian Buchwald, Sebastian Hack, Roland Leißa, Christoph Mallon, and Andreas Zwinkau. */

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

static bool has_long(LIST(long) list, long want)
{
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i] == want) {
			return true;
		}
	}
	return false;
}

static bool has_inst(LIST(ir_inst_t *) list, ir_inst_t *want)
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

/* The postorder computation was inspired from (here)[https://github.com/sampsyo/bril/blob/main/examples/dom.py#L34]. */

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
	reg_t *r = reg;
	while(r->ssareg) {
		r = r->ssareg;
	}
	return r;
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

static reg_t *try_remove_trivial_phi(ir_inst_t *phi)
{
	if(list_len(phi->phi_args) == 1) {
		phi->type = IR_INST_MOV;
		phi->r1 = phi->phi_args[0];
		list_delete(phi->phi_args);
		return phi->r0;
	}

	reg_t *same = NULL;
	for(size_t i = 0; i < list_len(phi->phi_args); i++) {
		reg_t *op = phi->phi_args[i];
		if(op == same || op == phi->r0) {
			continue;
		}

		if(!same) {
			/* phi merges at least 2 values */
			return phi->r0;
		}

		same = op;
	}

	if(!same) {
		/* DCE will take care of this. */
		return phi->r0;
	}

	/* replace all uses of `phi->r0` to `same` */
	phi->type = IR_INST_NOP;
	list_delete(phi->phi_args);

#define REPLACE(x)                       \
	do {                                 \
		if((x) && (x) == phi->r0) {      \
			if(!has_inst(users, ins)) {  \
				list_append(users, ins); \
			}                            \
			(x) = same;                  \
		}                                \
	} while(0)

	LIST(ir_inst_t *) users = list_make(ir_inst_t *);

	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			REPLACE(ins->r0);
			REPLACE(ins->r1);
			REPLACE(ins->r2);
			if(ins->type == IR_INST_CALL) {
				for(size_t j = 0; j < list_len(ins->call_args); j++) {
					REPLACE(ins->call_args[j]->r);
				}
			}
			if(ins->type == IR_INST_PHI) {
				for(size_t j = 0; j < list_len(ins->phi_args); j++) {
					REPLACE(ins->phi_args[j]);
				}
			}
		}
	}

	/* try recursively remove phi users */
	for(size_t i = 0; i < list_len(users); i++) {
		if(users[i]->type != IR_INST_PHI) {
			continue;
		}
		(void)try_remove_trivial_phi(users[i]);
	}

	list_delete(users);

	return same;
}

static reg_t *add_phi_ops(ir_blk_t *blk, reg_t *var, ir_inst_t *phi)
{
	for(size_t i = 0; i < list_len(blk->pred); i++) {
		list_append(phi->phi_args, read_reg_gvn(blk->pred[i], var));
	}
	return try_remove_trivial_phi(phi);
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

static reg_t *read_reg_gvn(ir_blk_t *blk, reg_t *reg)
{
	reg_t *var = find_var(reg);

	if(has_blkreg(var->blkregs, blk)) {
		return var->blkregs[find_blkreg(var->blkregs, blk)]->reg;
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

	sealed_blks = list_make(ir_blk_t *);

	/* append NOP to each block, find befores */
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ins_nop();
		nop->next = blk->insts;
		blk->insts = nop;
		find_before_last_term_ins(blk);
	}

	LIST(reg_t *) allocated = list_make(reg_t *);

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->r0 && !inst->r0->blkregs) {
				inst->r0->blkregs = list_make(blkreg_t *);
				list_append(allocated, inst->r0);
			}
		}
	}

	LIST(long) postorder = postorder_get(fun->blocks);

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
	for(size_t i = 0; i < list_len(postorder); i++) {
		seal_block(fun->blocks[i]);
	}
	ir_fix(fun);

	for(size_t i = 0; i < list_len(allocated); i++) {
		for(size_t j = 0; j < list_len(allocated[i]->blkregs); j++) {
			free(allocated[i]->blkregs[j]);
		}
		list_delete(allocated[i]->blkregs);
	}

	list_delete(sealed_blks);
	list_delete(postorder);
	list_delete(allocated);

	return;
}

static bool has_several_outgoing_edges(ir_inst_t *ins)
{
	if(ins->type == IR_INST_JMP || ins->type == IR_INST_RET) {
		return false;
	}

	/* else, if true block != false block, true */
	return ins->true_blk != ins->false_blk;
}

static void replace_edge(ir_blk_t *blk, ir_blk_t *orig, ir_blk_t *replace)
{
	ir_inst_t *last = blk->tail;
	if(last->true_blk && last->true_blk == orig) {
		last->true_blk = replace;
	}
	if(last->false_blk && last->false_blk == orig) {
		last->false_blk = replace;
	}
	return;
}

/* pre-step to destroying SSA form */
static void split_critical(ir_func_t *fun)
{
	/* append NOP to each block, find befores */
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ins_nop();
		nop->next = blk->insts;
		blk->insts = nop;
		find_before_last_term_ins(blk);
	}

	long counter = fun->blocks[list_len(fun->blocks) - 1]->num;
	size_t orig_len = list_len(fun->blocks);
	for(size_t i = 0; i < orig_len; i++) {
		ir_blk_t *blk = fun->blocks[i];

		bool has_phis = false;
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->type == IR_INST_PHI) {
				has_phis = true;
				break;
			}
		}

		if(!has_phis) {
			continue;
		}

		ir_inst_t **pmovs = zcalloc(list_len(blk->pred), sizeof(ir_inst_t *));

		for(size_t j = 0; j < list_len(blk->pred); j++) {
			ir_blk_t *pred = blk->pred[j];
			ir_inst_t *pmov = ir_inst_make(IR_INST_PMOV, NULL, NULL, NULL, 0);
			pmovs[j] = pmov;
			pmov->pmov_args = list_make(reg_pmov_t);
			/* TODO: this doesn't work.. why? */
			if(0 && has_several_outgoing_edges(pred->tail)) {
				ir_inst_t *jmp = ins_jmp(blk);
				pmov->next = jmp;
				ir_blk_t *newblk = ir_blk_make(pmov);
				newblk->num = ++counter;
				list_append(fun->blocks, newblk);
				replace_edge(pred, blk, newblk);
			} else {
				pred->tailprev->next = pmov;
				pmov->next = pred->tail;
				pred->tailprev = pred->tailprev->next;
			}
		}

		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->type != IR_INST_PHI) {
				continue;
			}

			inst->type = IR_INST_NOP;
			for(size_t j = 0; j < list_len(inst->phi_args); j++) {
				reg_pmov_t mov = { .dst = inst->r0, .src = inst->phi_args[j] };
				list_append(pmovs[j]->pmov_args, mov);
			}

			list_delete(inst->phi_args);
		}

		free(pmovs);
	}
}

static LIST(reg_pmov_t) deparallelize_pmov(ir_inst_t *pmov)
{
	UNUSEDA int useless = 0;
	for(size_t i = 0; i < list_len(pmov->pmov_args); i++) {
		reg_pmov_t arg = pmov->pmov_args[i];
		for(size_t j = 0; j < list_len(pmov->pmov_args); j++) {
			reg_pmov_t chk = pmov->pmov_args[j];
			if(chk.src == arg.dst) {
				reg_t *orig = arg.dst;
				arg.dst = reg_make();
				arg.dst->insty = IR_INST_PMOV;
				arg.dst->lhs = orig;
				pmov->pmov_args[i].dst = arg.dst;
				goto comehere;
			}
		}
comehere:
		useless = 0;
	}

	LIST(reg_pmov_t) seq = list_make(reg_pmov_t);

	for(size_t i = 0; i < list_len(pmov->pmov_args); i++) {
		list_append(seq, pmov->pmov_args[i]);
	}

	for(size_t i = 0; i < list_len(pmov->pmov_args); i++) {
		if(pmov->pmov_args[i].dst->insty == IR_INST_PMOV) {
			list_append(seq, ((reg_pmov_t){ .dst = pmov->pmov_args[i].dst->lhs,
											.src = pmov->pmov_args[i].dst }));
		}
	}

	list_delete(pmov->pmov_args);
	return seq;
}

/* turns parallel moves -> sequential moves */
static void deparallelize_pmovs(ir_func_t *fun)
{
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
		ir_inst_t *prev = blk->insts;
		ir_inst_t *nxt;
		for(ir_inst_t *inst = blk->insts->next; inst; inst = nxt) {
			nxt = inst->next;
			LIST(reg_pmov_t) seq = NULL;
			if(inst->type != IR_INST_PMOV) {
				goto end;
			}
			inst->type = IR_INST_NOP;

			seq = deparallelize_pmov(inst);
			ir_inst_delete(inst);
			if(list_len(seq) == 0) {
				list_delete(seq);
				goto end;
			}

			ir_inst_t *mov = ins_mov(NULL, NULL);
			ir_inst_t *prev_mov = NULL;
			ir_inst_t *newmov;
			prev->next = mov;
			for(size_t i = 0; i < list_len(seq); i++) {
				reg_pmov_t cur_mov = seq[i];
				mov->r0 = cur_mov.dst;
				mov->r1 = cur_mov.src;
				newmov = ins_mov(NULL, NULL);
				prev_mov = mov;
				mov->next = newmov;
				mov = newmov;
			}
			ir_inst_delete(newmov);
			prev_mov->next = nxt;
			list_delete(seq);

end:
			prev = inst;
		}
	}
}

/* turn the IR from an SSA form into a typical 3AC IR.
 * Removes and deallocates all phis, turning them into NOPs. */
void ir_ssa_exit(ir_func_t *fun)
{
	ir_blk_flow(fun);
	split_critical(fun);
	deparallelize_pmovs(fun);
	return;
}
