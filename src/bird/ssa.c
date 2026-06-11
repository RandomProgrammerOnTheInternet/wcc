#include "bird.h"

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

/* I'm using A Simple, Fast Dominance Algorithm by Keith D. Cooper, Timothy J. Harvey, and Ken Kennedy. */
/* The postorder computation was inspired from (here)[https://github.com/sampsyo/bril/blob/main/examples/dom.py#L34]. */
/* The computation was also inspired from (here)[https://github.com/Golf0ned/PANNICC/blob/main/src/middleend/analysis/dominator_tree.cpp]. */

static void postorder_visit(LIST(long) postorder, ir_blk_t *blk)
{
	if(blk->visited) {
		return;
	}

	blk->visited = true;
	list_append(postorder, blk->num);

	if(blk->tail->true_blk)
		postorder_visit(postorder, blk->tail->true_blk);
	if(blk->tail->false_blk)
		postorder_visit(postorder, blk->tail->false_blk);

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

static void fill_predeccessors(LIST(ir_blk_t *) blocks, LIST(long) postorder)
{
	for(size_t i = 0; i < list_len(postorder); i++) {
		ir_blk_t *blk = blocks[postorder[i]];
		for(size_t j = 0; j < list_len(blk->pred); j++) {
			ir_blk_t *pred = blk->pred[j];
			for(size_t k = 0; k < list_len(pred->pred); k++) {
				if(!has_blk(blk->pred, pred->pred[k])) {
					list_append(blk->pred, pred->pred[k]);
				}
			}
		}
	}
	return;
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

static long intersect(LIST(long) postorder, LIST(long) doms, long b1, long b2)
{
	while(b1 != b2) {
		while(postorder[b1] < postorder[b2]) {
			b1 = doms[b1];
		}
		while(postorder[b2] < postorder[b1]) {
			b2 = doms[b2];
		}
	}

	return b1;
}

static LIST(long) compute_dominators(LIST(ir_blk_t *) blocks)
{
	LIST(long) doms = list_make(long);
	LIST(long) postorder = postorder_get(blocks);
	LIST(long) rev_postorder = reverse_postorder(postorder);

	fill_predeccessors(blocks, postorder);

	for(size_t i = 0; i < list_len(rev_postorder); i++) {
		blocks[postorder[i]]->postnum = i;
	}
	for(size_t i = 0; i < list_len(blocks); i++) {
		list_append(doms, -1);
	}

	doms[0] = 0;

	bool changed = true;
	while(changed) {
		changed = false;
		/* ip = i' */
		for(size_t ip = 0; ip < list_len(rev_postorder); ip++) {
			size_t i = rev_postorder[ip];
			if(i == 0) {
				continue;
			}

			ir_blk_t *blk = blocks[i];
			long idom = -1;
			for(size_t j = 0; j < list_len(blk->pred); j++) {
				ir_blk_t *pred = blk->pred[j];
				if(!has_long(doms, pred->num)) {
					continue;
				}

				if(idom == -1) {
					idom = pred->num;
				} else {
					idom = intersect(postorder, doms, pred->num, idom);
				}
			}

			if(idom != -1 && doms[i] != idom) {
				doms[i] = idom;
				changed = true;
			}
		}
	}

	list_delete(postorder);
	list_delete(rev_postorder);

	return doms;
}

static void compute_dominance_frontier(LIST(ir_blk_t *) blocks, LIST(long) doms)
{
	for(size_t i = 0; i < list_len(blocks); i++) {
		ir_blk_t *blk = blocks[i];
		if(list_len(blk->pred) < 2) {
			continue;
		}

		for(size_t j = 0; j < list_len(blk->pred); j++) {
			ir_blk_t *pred = blk->pred[j];
			ir_blk_t *runner = pred;
			while(runner->num != doms[blk->num]) {
				list_append(runner->dom_frontier, blk->num);
				runner = blocks[doms[runner->num]];
			}
		}
	}
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

/* insert the phi nodes the big SSA lobbyists want us to */
static void insert_phi_node(ir_func_t *fun, reg_t *reg)
{
	LIST(ir_blk_t *) blocks = fun->blocks;
	LIST(ir_blk_t *) phiblks = list_make(ir_blk_t *);
	LIST(ir_blk_t *) defblks = list_make(ir_blk_t *);
	LIST(ir_blk_t *) workblks = list_make(ir_blk_t *);

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		if(block_contains_regdef(blk, reg)) {
			list_append(defblks, blk);
			list_append(workblks, blk);
		}
	}

	int64_t top = list_len(workblks) - 1;
	while(top != 0) {
		ir_blk_t *blk = workblks[--top];
		list_hdr(workblks)->size = top;
		for(size_t i = 0; i < list_len(blk->dom_frontier); i++) {
			ir_blk_t *dom = blocks[blk->dom_frontier[i]];
			if(!has_blk(phiblks, dom)) {
				list_append(phiblks, dom);

				ir_inst_t *phi = ir_inst_make(IR_INST_PHI, reg, NULL, NULL, 0);

				phi->phi_args = list_make(reg_t *);
				for(size_t i = 0; i < list_len(dom->pred); i++) {
					list_append(phi->phi_args, NULL);
				}

				phi->next = dom->insts->next;
				dom->insts->next = phi;

				if(!has_blk(defblks, dom)) {
					top++;
					list_append(workblks, dom);
				}
			}
		}
	}

	list_delete(phiblks);
	list_delete(defblks);
	list_delete(workblks);
}

void ir_ssa_enter(ir_func_t *fun)
{
	UNUSED(fun);
	ir_fix(fun);
	ir_nopremover(fun);
	ir_blk_flow(fun);

	/* append NOP to each block, find befores */
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ins_nop();
		nop->next = blk->insts;
		blk->insts = nop;
		find_before_last_term_ins(blk);
	}
	LIST(long) doms = compute_dominators(fun->blocks);
	ir_blk_flow(fun);
	compute_dominance_frontier(fun->blocks, doms);

	LIST(reg_t *) allregs = list_make(reg_t *);
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			if(inst->r0 && !has_reg(allregs, inst->r0)) {
				list_append(allregs, inst->r0);
			}
		}
	}

	for(size_t i = 0; i < list_len(allregs); i++) {
		insert_phi_node(fun, allregs[i]);
	}

	ir_nopremover(fun);
	ir_dump(fun, 'v');
	exit(1);

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
