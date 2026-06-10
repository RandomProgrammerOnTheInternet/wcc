#include "ir.h"
#include "regalloc.h"

/* simple linear search */
static int has_reg(LIST(reg_t *) list, reg_t *target)
{
	for(size_t i = 0; i < list_len(list); i++) {
		if(list[i] == target) {
			return 1;
		}
	}
	return 0;
}

static ir_inst_t *find_last_or_flow_ins(ir_inst_t *root)
{
	ir_inst_t *ret = root;
	for(; ret; ret = ret->next) {
		if(ir_inst_is_term(ret->type)) {
			return ret;
		}
	}
	ASSERT(ir_inst_is_term(ret->type), "IR is not constructed properly");
	return NULL;
}

/* fill out all the defined registers in this block */
static void fill_defs(ir_blk_t *blk)
{
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		if(!ins->r0) {
			continue;
		}
		if(!has_reg(blk->regs_def, ins->r0)) {
			list_append(blk->regs_def, ins->r0);
		}
	}
	return;
}

/* fill out successors and predecessors (and also if block returns) */
static void fill_succ_pred(ir_blk_t *blk)
{
	if(!blk || blk->visited) {
		return;
	}

	blk->visited = true;

	/* find the last inst. */
	ir_inst_t *flow = find_last_or_flow_ins(blk->insts);
	if(flow->type == IR_INST_RET) {
		blk->returns = true;
		return;
	}

	if(flow->false_blk) {
		/* add successor, predecessor */
		list_append(flow->false_blk->pred, blk);
		fill_succ_pred(flow->false_blk);
	}

	if(flow->true_blk) {
		/* add successor, predecessor */
		list_append(flow->true_blk->pred, blk);
		fill_succ_pred(flow->true_blk);
	}

	return;
}

static void fill_ins_outs_reg(ir_blk_t *blk, reg_t *reg)
{
	if(!blk || !reg) {
		return;
	}

	/* want to make sure it's not a defined reg */
	if(has_reg(blk->regs_def, reg)) {
		return;
	}

	/* its an input reg */
	if(!has_reg(blk->regs_in, reg)) {
		list_append(blk->regs_in, reg);
	} else {
		return;
	}

	/* to the predeccesors it's also an output reg: add it there */
	for(size_t i = 0; i < list_len(blk->pred); i++) {
		if(has_reg(blk->pred[i]->regs_def, reg) &&
		   !has_reg(blk->pred[i]->regs_out, reg)) {
			list_append(blk->pred[i]->regs_out, reg);
			fill_ins_outs_reg(blk->pred[i], reg);
		}
	}

	return;
}

/* fill the input & output registers of block */
static void fill_ins_outs(ir_blk_t *blk)
{
	for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
		/* r0 is never an in/out reg because it is generated in the block. */
		if(inst->r1)
			fill_ins_outs_reg(blk, inst->r1);
		if(inst->r2)
			fill_ins_outs_reg(blk, inst->r2);

		/* call arguments */
		if(inst->type == IR_INST_CALL) {
			for(size_t i = 0; i < list_len(inst->call_args); i++) {
				fill_ins_outs_reg(blk, inst->call_args[i]->r);
			}
		}
	}
}

static void reset_blk(ir_blk_t *blk)
{
	list_hdr(blk->pred)->size = 0;
	list_hdr(blk->dom_frontier)->size = 0;
	list_hdr(blk->regs_def)->size = 0;
	list_hdr(blk->regs_in)->size = 0;
	list_hdr(blk->regs_out)->size = 0;
}

static void reset_fun(ir_func_t *fun)
{
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		reset_blk(fun->blocks[i]);
		fun->blocks[i]->visited = false;
	}
}

void ir_blk_reguse(ir_func_t *fun)
{
	reset_fun(fun);
	size_t block_amount = list_len(fun->blocks);
	fill_succ_pred(fun->blocks[0]);
	for(size_t i = 0; i < block_amount; i++) {
		fill_defs(fun->blocks[i]);
	}
	for(size_t i = 0; i < block_amount; i++) {
		fill_ins_outs(fun->blocks[i]);
	}
	return;
}

void ir_blk_flow(ir_func_t *fun)
{
	reset_fun(fun);
	size_t block_amount = list_len(fun->blocks);
	fill_succ_pred(fun->blocks[0]);
	for(size_t i = 0; i < block_amount; i++) {
		fill_defs(fun->blocks[i]);
	}
	for(size_t i = 0; i < block_amount; i++) {
		fill_ins_outs(fun->blocks[i]);
	}
	return;
}

/* needs ir_blk_reguse; defines the input registers to be zero for the entry block */
void ir_blk_fixup_entry(ir_func_t *fun)
{
	if(list_len(fun->blocks) == 0) {
		return;
	}
	ir_blk_t *entry = fun->blocks[0];
	ir_inst_t *nop = ir_inst_make(IR_INST_NOP, NULL, NULL, NULL, 0);
	nop->next = entry->insts;
	entry->insts = nop;

	ir_inst_t *prev = entry->insts;
	ir_inst_t *cur = entry->insts->next;

	for(size_t i = 0; i < list_len(entry->regs_in); i++) {
		reg_t *reg = entry->regs_in[i];
		ir_inst_t *def = ir_inst_make(IR_INST_IMM, reg, NULL, NULL, 0);
		prev->next = def;
		def->next = cur;
		prev = cur;
		cur = cur->next;
	}

	entry->insts = entry->insts->next;
	ir_inst_delete(nop);

	/* recompute defs */
	fill_defs(entry);

	return;
}

/* assumes the register is either r1 or r2 */
static void reg_update_counter(reg_t *reg, long ins_counter)
{
	if(!reg)
		return;
	if(reg->last_use < ins_counter) {
		reg->last_use = ins_counter;
	}
	if(reg->no_mov_elim) {
		reg->last_use = INT64_MAX;
	}
	return;
}

/* calculates register defs & last use for all blocks in `fun` */
LIST(reg_t *) ir_blk_reglive(ir_func_t *fun)
{
	LIST(reg_t *) allocated = list_make(reg_t *);
	/* the algorithm here is quite simple. basically,
	 * we assume the blocks are laid out in order,
	 * and then for each instruction we increment a counter
	 * and then that is the "Program Counter". We assign
	 * register definitions & last uses based on that number.
	 * We also do these for the output registers.
	 * This will be used for the eventual register allocator.
	 */

	/* instruction counter, starts at 1 because 0 is undef'd */
	long ins_count = 1;
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			reg_update_counter(ins->r0, ins_count);
			if(ins->r0 && ins->r0->def == 0) {
				ins->r0->def = ins_count;
				list_append(allocated, ins->r0);
			}
			reg_update_counter(ins->r1, ins_count);
			reg_update_counter(ins->r2, ins_count);

			if(ins->type == IR_INST_CALL) {
				for(size_t j = 0; j < list_len(ins->call_args); j++) {
					reg_t *reg = ins->call_args[j]->r;
					reg_update_counter(reg, ins_count);
				}
			}

			ins_count++;
		}
		for(size_t j = 0; j < list_len(blk->regs_out); j++) {
			reg_update_counter(blk->regs_out[j], ins_count);
		}
	}
	return allocated;
}

static int spill_register(reg_t **regs, int amount)
{
	/* choose the one with the last last use */
	int reg = 0;
	for(size_t i = 0; i < (size_t)amount; i++) {
		if(regs[reg]->last_use < regs[i]->last_use) {
			reg = i;
		}
	}
	return reg;
}

/* spill a register read `reg` before `ins` */
static void rewrite_load_spill(ir_inst_t *ins_prev, ir_inst_t *ins, reg_t *reg)
{
	ASSERT(reg->spilld, "tried to spill a non-spilled register");
	ir_inst_t *inst = ir_inst_make(IR_INST_LOADSS, reg, NULL, NULL, reg->off);
	inst->size = 8;
	ins_prev->next = inst;
	inst->next = ins;
	return;
}

/* insert a spilled store after `ins` */
static void rewrite_store_spill(ir_inst_t *ins)
{
	ASSERT(ins->r0->spilld, "tried to spill a non-spilled register");
	ir_inst_t *inst =
		ir_inst_make(IR_INST_STORESS, NULL, ins->r0, NULL, ins->r0->off);
	inst->size = 8;
	ir_inst_t *nxt = ins->next;
	ins->next = inst;
	inst->next = nxt;
	return;
}

/* turns A = F(B, C) where they are all spilled into A = B; A = F(A, C) */
static void rewrite_make_2op(ir_inst_t *ins_prev, ir_inst_t *ins)
{
	ir_inst_t *mov = ins_mov(ins->r0, ins->r1);
	ins->r1 = ins->r0;
	ins_prev->next = mov;
	mov->next = ins;
	return;
}

/* spill registers used in `ins` if needed */
static void rewrite_ins(ir_inst_t *ins_prev, ir_inst_t *ins)
{
	if(ins->type == IR_INST_LOADSS || ins->type == IR_INST_STORESS) {
		return;
	}

	/* edge case */
	if(ins->r0 && ins->r1 && ins->r2 && ins->r0->spilld && ins->r1->spilld &&
	   ins->r2->spilld) {
		rewrite_make_2op(ins_prev, ins);
		ins_prev = ins_prev->next;
	}

	if(ins->r0 && ins->r0->spilld) {
		rewrite_store_spill(ins);
	}

	if(ins->r1 && ins->r1->spilld) {
		rewrite_load_spill(ins_prev, ins, ins->r1);
		ins_prev = ins_prev->next;
	}

	if(ins->r2 && ins->r2->spilld) {
		rewrite_load_spill(ins_prev, ins, ins->r2);
		ins_prev = ins_prev->next;
	}

	return;
}

/* spill the needed registers to spill */
void ir_regalloc_spill(ir_func_t *fun, LIST(reg_t *) allocated)
{
	/* now, calculate the spill offsets */
	long off = -(long)(fun->stack_needed);
	for(size_t i = 0; i < list_len(allocated); i++) {
		if(!allocated[i]->spilld) {
			continue;
		}
		reg_t *r = allocated[i];
		off -= 8;
		fun->stack_needed += 8;
		r->off = off;
	}

	/* rewriting */

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ir_inst_make(IR_INST_NOP, NULL, NULL, NULL, 0);
		nop->next = blk->insts;
		blk->insts = nop;

		ir_inst_t *prev = blk->insts;
		ir_inst_t *cur = blk->insts->next;
		for(; cur; cur = cur->next) {
			rewrite_ins(prev, cur);
			prev = cur;

			if(ir_inst_is_term(cur->type)) {
				break;
			}
		}

		blk->insts = blk->insts->next;
		ir_inst_delete(nop);
	}
}

/* do the register allocation on `fun` */
void ir_regalloc(LIST(reg_t *) allocated, int amount_)
{
	size_t amount = amount_ - 1; /* reserve 1 register for spilling */

	/* register allocation: simple linear scan */

	/* real registers */
	reg_t **regs = zcalloc((size_t)amount_, sizeof(reg_t *));

	for(size_t i = 0; i < list_len(allocated); i++) {
		reg_t *r = allocated[i];
		if(!r) {
			/* can't really spill a NULL */
			continue;
		}

		bool need_spill = true;
		/* find a register */
		for(size_t j = 0; j < amount; j++) {
			if(regs[j] && regs[j]->last_use > r->def) {
				continue;
			}

			need_spill = false;
			r->rr = j;
			r->spilld = false;
			regs[j] = r;
			break;
		}

		if(!need_spill)
			continue;

		/* spill a register */
		regs[amount] = r;
		int spill = spill_register(regs, amount + 1);
		r->rr = spill;
		regs[spill]->spilld = true;
		regs[spill]->rr = amount;
		regs[spill] = r;
	}

	free(regs);
	return;
}

static long ir_eval_strat_cost(ir_func_t *fun, int amount, int callee_cost,
							   int caller_cost, bool strat, bool *used)
{
	long cost = 0;

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			if(ins->r0) {
				used[ins->r0->rr] = 1;
			}

			if(ins->r1) {
				used[ins->r1->rr] = 1;
			}

			if(ins->r2) {
				used[ins->r2->rr] = 1;
			}

			if(ins->type != IR_INST_CALL) {
				continue;
			}

			size_t args = list_len(ins->call_args);
			for(size_t i = 0; i < args; i++) {
				if(ins->call_args[i]) {
					used[ins->call_args[i]->r->rr] = 1;
				}
			}

			/* count # of registers used */
			size_t used_count = 0;
			size_t leftovers = 0;
			for(size_t i = 0; i < (size_t)amount; i++) {
				used_count += used[i];
			}
			if(used_count > (size_t)caller_cost) {
				leftovers = used_count - caller_cost;
				used_count = caller_cost;
			}

			if(!strat) {
				cost += used_count;
			} else {
				cost += leftovers;
			}
		}
	}

	size_t used_count = 0;
	size_t leftovers = 0;
	for(size_t i = 0; i < (size_t)amount; i++) {
		used_count += used[i];
	}
	if(used_count > (size_t)callee_cost) {
		leftovers = used_count - callee_cost;
		used_count = callee_cost;
	}

	if(strat) {
		cost += used_count;
	} else {
		cost += leftovers;
	}

	return cost;
}

/* chooses a register allocation strat */
/* callee_cost = # of registers that are callee-save */
/* caller_cost = # of registers that are caller-save */
static bool ir_choose_alloc_strat(ir_func_t *fun, int amount, int callee_cost,
								  int caller_cost)
{
	bool *used = zcalloc(amount, sizeof(bool));
	fun->alloc_used = used;
	long cost_caller =
		ir_eval_strat_cost(fun, amount, callee_cost, caller_cost, false, used);
	long cost_callee =
		ir_eval_strat_cost(fun, amount, callee_cost, caller_cost, true, used);
	if(cost_callee == cost_caller) {
		/* callee is easier to implement */
		return true;
	}

	if(cost_callee < cost_caller) {
		return true; /* callee-save */
	} else if(cost_caller < cost_callee) {
		return false; /* caller-save */
	}

	return true; /* how did you get here? */
}

static void ir_simplify(ir_func_t *fun, int amount)
{
	long *imm = zcalloc(amount, sizeof(long));
	bool *are_imm = zcalloc(amount, sizeof(bool));
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		for(size_t i = 0; i < (size_t)amount; i++) {
			are_imm[i] = false;
			imm[i] = 0;
		}
		ir_blk_t *blk = fun->blocks[i];

		ir_inst_t *nop = ir_inst_make(IR_INST_NOP, NULL, NULL, NULL, 0);
		nop->next = blk->insts;
		blk->insts = nop;

		ir_inst_t *prev = nop;
		ir_inst_t *nxt = blk->insts;
		int removed = 0;
		for(ir_inst_t *ins = blk->insts; ins; ins = nxt) {
			removed = 0;
			nxt = ins->next;

			/* simplify useless moves where the source
			 * and destination have same real register */
			if(ins->type == IR_INST_MOV && ins->r0->rr != -1 &&
			   ins->r0->rr == ins->r1->rr) {
				prev->next = nxt;
				ir_inst_delete(ins);
				ins = nxt;
				removed = 1;
				goto end;
			}

			/* remove useless insts where thing stored is never used
			 * beyond this inst */
			if(ins->r0 && ins->r0->def == ins->r0->last_use &&
			   ins->type != IR_INST_CALL) {
				prev->next = nxt;
				ir_inst_delete(ins);
				ins = nxt;
				removed = 1;
				goto end;
			}

			/* if call ins, and the val is not used at all, remove
			 * the storing */
			if(ins->r0 && ins->r0->def == ins->r0->last_use &&
			   ins->type == IR_INST_CALL) {
				ins->r0 = NULL;
			}

			/* seed immediate values */
			if(ins->type == IR_INST_IMM) {
				/* remove useless immediate loads */
				if(are_imm[ins->r0->rr] &&
				   ins->imm == (uint64_t)imm[ins->r0->rr]) {
					prev->next = nxt;
					ir_inst_delete(ins);
					ins = nxt;
					removed = 1;
					goto end;
				}
				are_imm[ins->r0->rr] = 1;
				imm[ins->r0->rr] = ins->imm;
			} else if(ins->r0) {
				are_imm[ins->r0->rr] = 0;
			}

end:
			if(!removed) {
				prev = ins;
			}
		}
		blk->insts = blk->insts->next;
		ir_inst_delete(nop);
	}

	free(imm);
	free(are_imm);
}

extern int debug;

/* do register allocation all in one */
void ir_finalize(ir_func_t *fun, int amount, enum ir_arch arch)
{
	ir_blk_reguse(fun);
	ir_blk_fixup_entry(fun);
	LIST(reg_t *) allocated = ir_blk_reglive(fun);
	ir_regalloc(allocated, amount);
	ir_fix(fun);
	ir_regalloc_spill(fun, allocated);
	ir_fix(fun);
	ir_simplify(fun, amount);
	ir_fix(fun);

	int callee_cost;
	int caller_cost;

	switch(arch) {
	case IR_ARCH_AARCH64_APPLE:
		callee_cost = 9; /* r19 .. r28 */
		caller_cost = 7; /* r9 .. r15 */
		break;
	case IR_ARCH_X64_SYSV:
		callee_cost = 5; /* rbx, r12 .. r15 */
		caller_cost = 6; /* rsi, rdx, rcx, r8, r9, r11 */
		break;

	default:
		ERROR("unknown backend");
		break;
	}

	(void)ir_choose_alloc_strat(fun, amount, callee_cost, caller_cost);

	/* free objects */
	for(size_t i = 0; i < list_len(allocated); i++) {
		if(!allocated[i]->spilld) {
			continue;
		}
	}

	list_delete(allocated);
	return;
}
