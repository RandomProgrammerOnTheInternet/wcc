#include "ir.h"

/* is the instruction in form A = F(B, C) where it needs A and B to be seperate? */
static bool ins_is_3source(enum ins_type t)
{
	/* ADD is not needed here because on x64 you can do lea A, [B+C] */
	return t == IR_INST_SUB || t == IR_INST_MUL || t == IR_INST_DIV;
}

/* is the instruction in form A = F(B) where it needs A and B to be seperate? */
static bool ins_is_2source(enum ins_type t)
{
	return t == IR_INST_NEG;
}

static void turn_into_x64_ins(ir_inst_t *prev, ir_inst_t *cur)
{
	if(!ins_is_3source(cur->type) && !ins_is_2source(cur->type)) {
		return;
	}

	if(ins_is_3source(cur->type)) {
		/* rewrite A = F(B, C) into A = B; A = F(A, C) */
		ir_inst_t *mov = ins_mov(cur->r0, cur->r1);
		cur->r1 = cur->r0;
		mov->next = cur;
		prev->next = mov;
		return;
	}

	if(ins_is_2source(cur->type)) {
		/* rewrite A = F(B) into A = B; A = F(A) */
		ir_inst_t *mov = ins_mov(cur->r0, cur->r1);
		cur->r1 = cur->r0;
		mov->next = cur;
		prev->next = mov;
		return;
	}
}

static void ir_turn_into_x64(ir_func_t *fun)
{
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nop = ir_inst_make(IR_INST_NOP, NULL, NULL, NULL, 0);
		nop->next = blk->insts;
		blk->insts = nop;

		ir_inst_t *prev = blk->insts;
		ir_inst_t *cur = blk->insts->next;
		for(; cur; cur = cur->next) {
			turn_into_x64_ins(prev, cur);
			prev = cur;

			if(ir_inst_is_term(cur->type)) {
				break;
			}
		}

		blk->insts = blk->insts->next;
		ir_inst_delete(nop);
	}
}

void ir_func_opt_x64(ir_func_t *fun, int opt_level)
{
	UNUSED(opt_level);
	/* TODO: immediate inc/dec optimization */
	ir_turn_into_x64(fun);
	return;
}

/* todo: some of these are argument registers, have to save them for later */
static const char *x64_reg[5] = { "rdi", "rsi", "rdx", "rcx", "r8" };

static int64_t i64abs(int64_t v)
{
	if(v < 0) {
		return -v;
	}
	return v;
}

static void ir_emit_blk_x64_sysv(FILE *f, ir_func_t *fn, ir_blk_t *blk,
								 long last_i)
{
	fprintf(f, ".BB%ld:\n", blk->num);
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		const char *r0 = ins->r0 && ins->r0->rr >= 0 ? x64_reg[ins->r0->rr] :
													   NULL;
		const char *r1 = ins->r1 && ins->r1->rr >= 0 ? x64_reg[ins->r1->rr] :
													   NULL;
		const char *r2 = ins->r2 && ins->r2->rr >= 0 ? x64_reg[ins->r2->rr] :
													   NULL;
		switch(ins->type) {
		case IR_INST_BREQ:
		case IR_INST_BRNE:
		case IR_INST_BRLT:
		case IR_INST_BRLE:
			fprintf(f, "\tcmp %s, %s\n", r1, r2);
			switch(ins->type) {
			default:
				break;
			case IR_INST_BREQ:
				fprintf(f, "\tjne .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRNE:
				fprintf(f, "\tje .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLT:
				fprintf(f, "\tjge .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLE:
				fprintf(f, "\tjg .BB%ld\n", ins->false_blk->num);
				break;
			}
			if(ins->true_blk->num == blk->num + 1) {
				/* fallthrough */
				break;
			}
			/* dang it */
			fprintf(f, "\tjmp .BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_BR:
			fprintf(f, "\ttest %s, %s\n", r1, r1);
			fprintf(f, "\tje .BB%ld\n", ins->false_blk->num);
			/* big brain optimization */
			/* fallthrough to true block if in front of us */
			if(ins->true_blk->num == blk->num + 1) {
				break;
			}
			/* else don't */
			fprintf(f, "\tjmp .BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_JMP:
			/* big brain optimization */
			/* fallthrough if target in front of us */
			if(ins->true_blk->num == blk->num + 1) {
				break;
			}
			/* else just jump */
			fprintf(f, "\tjmp .BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_RET:
			if(r1 != NULL) {
				fprintf(f, "\tmov rax, %s\n", r1);
			}
			if(blk->num != last_i) {
				fprintf(f, "\tjmp %s_ret\n", fn->name);
			}
			break;
		case IR_INST_NOP:
			break;
		case IR_INST_MOV:
			fprintf(f, "\tmov %s, %s\n", r0, r1);
			break;
		case IR_INST_IMM:
			if(ins->imm == 0) {
				fprintf(f, "\txor %s, %s\n", r0, r0);
			} else {
				fprintf(f, "\tmov %s, %lld\n", r0, (int64_t)ins->imm);
			}
			break;
		case IR_INST_ADD:
			if(ins->r1->rr != ins->r0->rr) {
				fprintf(f, "\tlea %s, [%s + %s]\n", r0, r1, r2);
			} else {
				fprintf(f, "\tadd %s, %s\n", r0, r2);
			}
			break;
		case IR_INST_SUB:
			fprintf(f, "\tsub %s, %s\n", r0, r2);
			break;
		case IR_INST_MUL:
			fprintf(f, "\timul %s, %s\n", r0, r2);
			break;
		case IR_INST_DIV:
			fprintf(f, "\tpush rdx\n");
			fprintf(f, "\tmov rax, %s\n", r0);
			fprintf(f, "\tcqo\n");
			fprintf(f, "\tidiv %s\n", r2);
			fprintf(f, "\tpop rdx\n");
			fprintf(f, "\tmov %s, rax\n", r0);
			break;
		case IR_INST_NEG:
			fprintf(f, "\tneg %s\n", r0);
			break;
		case IR_INST_EQ:
		case IR_INST_NE:
		case IR_INST_LT:
		case IR_INST_LE:
			fprintf(f, "\tcmp %s, %s\n", r1, r2);

			switch(ins->type) {
			case IR_INST_EQ:
				fprintf(f, "\tsete al\n");
				break;
			case IR_INST_NE:
				fprintf(f, "\tsetne al\n");
				break;
			case IR_INST_LT:
				fprintf(f, "\tsetl al\n");
				break;
			case IR_INST_LE:
				fprintf(f, "\tsetle al\n");
				break;

			default: /* wth? */
				break;
			}
			fprintf(f, "\tmovzx %s, al\n", r0);
			break;
		case IR_INST_LEAS:
			fprintf(f, "\tlea %s, [rbp - %lld]\n", r0, (int64_t)ins->imm);
			break;
		case IR_INST_LOAD:
			fprintf(f, "\tmov %s, [%s]\n", r0, r1);
			break;
		case IR_INST_LOADS:
		case IR_INST_LOADSS:
			fprintf(f, "\tmov %s, [rbp - %lld]\n", r0,
					i64abs((int64_t)ins->imm));
			break;
		case IR_INST_STORE:
			fprintf(f, "\tmov [%s], %s\n", r1, r2);
			break;
		case IR_INST_STORES:
		case IR_INST_STORESS:
			fprintf(f, "\tmov [rbp - %lld], %s\n", i64abs((int64_t)ins->imm),
					r1);
			break;

		default:
			break;
		}

		if(ir_inst_is_term(ins->type)) {
			break;
		}
	}
	return;
}

void ir_func_emit_x64_sysv(FILE *f, ir_func_t *fun)
{
	char *name = fun->name;
	char *og_name = name;
	/* big no no on x64 */
	if(starts_with(name, "_")) {
		name = name + 1;
		fun->name = name;
	}
	/* correct syntax */
	fprintf(f, ".intel_syntax noprefix\n");
	fprintf(f, ".global %s\n", name);
	fprintf(f, ".align 4\n");
	fprintf(f, "%s:\n", name);

	/* enter stack frame */
	size_t alignd = align_to(fun->stack_needed, 16);
	fprintf(f, "\tpush rbp\n");
	fprintf(f, "\tmov rbp, rsp\n");
	if(alignd) {
		fprintf(f, "\tsub rsp, %zu\n", alignd);
	}

	size_t len = list_len(fun->blocks);
	for(size_t i = 0; i < len; i++) {
		ir_emit_blk_x64_sysv(f, fun, fun->blocks[i], len - 1);
	}

	/* leave stack frame */
	fprintf(f, "%s_ret:\n", name);
	fprintf(f, "\tmov rsp, rbp\n");
	fprintf(f, "\tpop rbp\n");
	fprintf(f, "\tret\n");

	fun->name = og_name;
	return;
}
