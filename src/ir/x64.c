#include "ir.h"
#include "x64.h"

static int unpromote(enum ins_type type)
{
	switch(type) {
	case IR_INST_ADDI:
		return IR_INST_ADD;
	case IR_INST_SUBI:
		return IR_INST_SUB;
	case IR_INST_MULI:
		return IR_INST_MUL;
	case IR_INST_DIVI:
		return IR_INST_DIV;
	case IR_INST_EQI:
		return IR_INST_EQ;
	case IR_INST_NEI:
		return IR_INST_NE;
	case IR_INST_LTI:
		return IR_INST_LT;
	case IR_INST_LEI:
		return IR_INST_LE;
	case IR_INST_GTI:
		return IR_INST_GT;
	case IR_INST_GEI:
		return IR_INST_GE;
	case IR_INST_BREQI:
		return IR_INST_BREQ;
	case IR_INST_BRNEI:
		return IR_INST_BRNE;
	case IR_INST_BRLTI:
		return IR_INST_BRLT;
	case IR_INST_BRLEI:
		return IR_INST_BRLE;
	case IR_INST_BRGTI:
		return IR_INST_BRGT;
	case IR_INST_BRGEI:
		return IR_INST_BRGE;
	default:
		return IR_INST_NOP;
	}
	return IR_INST_NOP;
}

/* remove immediate from ins if immediate is out of [-4095, 4095] */
/* for mul & div, remove it anyway */
/* also turn addi x, neg -> subi x, pos
 *           subi x, neg -> addi x, pos */
static void degrade_ins(ir_inst_t *prev, ir_inst_t *ins)
{
	if(!ir_inst_r2_imm(ins->type)) {
		return;
	}

	if(ins->type == IR_INST_ADDI && (int64_t)ins->imm < 0) {
		ins->type = IR_INST_SUBI;
		ins->imm = -(int64_t)ins->imm;
	}

	if(ins->type == IR_INST_SUBI && (int64_t)ins->imm < 0) {
		ins->type = IR_INST_ADDI;
		ins->imm = -(int64_t)ins->imm;
	}

	int64_t imm = (int64_t)ins->imm;
	if(ins->type == IR_INST_MULI || ins->type == IR_INST_DIVI) {
		goto remove;
	}

	if(imm < INT32_MIN || imm > INT32_MAX) {
		reg_t *imml;
remove:
		imml = reg_make();
		ir_inst_t *new_ins = ins_imm(imml, imm);
		new_ins->next = ins;
		prev->next = new_ins;
		ins->type = unpromote(ins->type);
		ins->r2 = imml;
	}

	return;
}

static void degrade_large_imms(ir_func_t *fun)
{
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];

		ir_inst_t *nop = ir_inst_make(IR_INST_NOP, NULL, NULL, NULL, 0);
		nop->next = blk->insts;
		blk->insts = nop;

		ir_inst_t *prev = nop;
		ir_inst_t *cur = blk->insts->next;
		for(; cur; cur = cur->next) {
			degrade_ins(prev, cur);
			prev = cur;

			if(ir_inst_is_term(cur->type)) {
				break;
			}
		}

		blk->insts = blk->insts->next;
		ir_inst_delete(nop);
	}
}

/* is the instruction in form A = F(B, C) where it needs A and B to be seperate? */
static bool ins_is_3source(enum ins_type t)
{
	/* ADD is not needed here because on x64 you can do lea A, [B+C] */
	return t == IR_INST_SUB || t == IR_INST_MUL || t == IR_INST_DIV ||
		   t == IR_INST_MULI || t == IR_INST_DIVI;
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
	degrade_large_imms(fun);
	/* TODO: immediate inc/dec optimization */
	ir_turn_into_x64(fun);
	return;
}

static const char *x64_reg[5] = { "rbx", "r12", "r13", "r14", "r15" };
static const char *x64_reg8[5] = { "bl", "r12b", "r13b", "r14b", "r15b" };
static const char *x64_reg16[5] = { "bx", "r12w", "r13w", "r14w", "r15w" };
static const char *x64_reg32[5] = { "ebx", "r12d", "r13d", "r14d", "r15d" };

static const int x64_reg_count = 5;

static int64_t i64abs(int64_t v)
{
	if(v < 0) {
		return -v;
	}
	return v;
}

static const char *arg_reg[6] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

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
		const char *r0b = ins->r0 && ins->r0->rr >= 0 ? x64_reg8[ins->r0->rr] :
														NULL;
		const char *r1b = ins->r1 && ins->r1->rr >= 0 ? x64_reg8[ins->r1->rr] :
														NULL;
		const char *r2b = ins->r2 && ins->r2->rr >= 0 ? x64_reg8[ins->r2->rr] :
														NULL;
		const char *r0w = ins->r0 && ins->r0->rr >= 0 ? x64_reg16[ins->r0->rr] :
														NULL;
		const char *r1w = ins->r1 && ins->r1->rr >= 0 ? x64_reg16[ins->r1->rr] :
														NULL;
		const char *r2w = ins->r2 && ins->r2->rr >= 0 ? x64_reg16[ins->r2->rr] :
														NULL;
		const char *r0d = ins->r0 && ins->r0->rr >= 0 ? x64_reg32[ins->r0->rr] :
														NULL;
		const char *r1d = ins->r1 && ins->r1->rr >= 0 ? x64_reg32[ins->r1->rr] :
														NULL;
		const char *r2d = ins->r2 && ins->r2->rr >= 0 ? x64_reg32[ins->r2->rr] :
														NULL;
		switch(ins->type) {
		case IR_INST_ZEXT: {
			switch(ins->size) {
			case 8:
				fprintf(f, "mov %s, %s\n", r0, r1);
				break;
			case 4:
				fprintf(f, "mov %s, %s\n", r0d, r1d);
				break;
			case 2:
				fprintf(f, "movzx %s, %s\n", r0d, r1w);
				break;
			case 1:
				fprintf(f, "movzx %s, %s\n", r0d, r1b);
				break;
			}
		}; break;
		case IR_INST_SEXT: {
			switch(ins->size) {
			case 8:
				fprintf(f, "mov %s, %s\n", r0, r1);
				break;
			case 4:
				fprintf(f, "movsxd %s, %s\n", r0, r1d);
				break;
			case 2:
				fprintf(f, "movsx %s, %s\n", r0, r1w);
				break;
			case 1:
				fprintf(f, "movsx %s, %s\n", r0, r1b);
				break;
			}
		}; break;
		case IR_INST_CALL: {
			size_t stack_used = 0;
			for(size_t i = 0; i < list_len(ins->call_args); i++) {
				if(ins->call_args[i]->spilld) {
					fprintf(f, "\tmov %s, [rbp - %lld]\n",
							x64_reg[ins->call_args[i]->rr],
							i64abs(ins->call_args[i]->off));
				}
				if(i < 6) {
					fprintf(f, "\tmov %s, %s\n", arg_reg[i],
							x64_reg[ins->call_args[i]->rr]);
				} else {
					fprintf(f, "\tpush %s\n", x64_reg[ins->call_args[i]->rr]);
					stack_used += 8;
				}
			}
			fprintf(f, "\tcall %s\n", ins->fname);
			if(r0) {
				fprintf(f, "\tmov %s, rax\n", r0);
			}
			if(stack_used) {
				fprintf(f, "\tsub rsp, %zu\n", stack_used);
			}
		} break;
		case IR_INST_BREQI:
		case IR_INST_BRNEI:
		case IR_INST_BRLTI:
		case IR_INST_BRLEI:
		case IR_INST_BRGTI:
		case IR_INST_BRGEI:
			fprintf(f, "\tcmp %s, %lld\n", r1, (int64_t)ins->imm);
			goto brcmp_main;

		case IR_INST_BREQ:
		case IR_INST_BRNE:
		case IR_INST_BRLT:
		case IR_INST_BRLE:
		case IR_INST_BRGT:
		case IR_INST_BRGE:
			fprintf(f, "\tcmp %s, %s\n", r1, r2);
brcmp_main:
			switch(ins->type) {
			default:
				break;
			case IR_INST_BREQ:
			case IR_INST_BREQI:
				fprintf(f, "\tjne .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRNE:
			case IR_INST_BRNEI:
				fprintf(f, "\tje .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLT:
			case IR_INST_BRLTI:
				fprintf(f, "\tjge .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLE:
			case IR_INST_BRLEI:
				fprintf(f, "\tjg .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRGT:
			case IR_INST_BRGTI:
				fprintf(f, "\tjle .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRGE:
			case IR_INST_BRGEI:
				fprintf(f, "\tjl .BB%ld\n", ins->false_blk->num);
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
				fprintf(f, "\txor %s, %s\n", r0d, r0d);
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
		case IR_INST_ADDI:
			if(ins->r0->rr == ins->r1->rr) {
				if(ins->imm == 1) {
					fprintf(f, "\tinc %s\n", r0);
				} else {
					fprintf(f, "\tadd %s, %lld\n", r0, (int64_t)ins->imm);
				}
				break;
			} else {
				fprintf(f, "\tlea %s, [%s + %lld]\n", r0, r1,
						(int64_t)ins->imm);
				break;
			}

		case IR_INST_SUBI:
			if(ins->r0->rr == ins->r1->rr) {
				if(ins->imm == 1) {
					fprintf(f, "\tdec %s\n", r0);
				} else {
					fprintf(f, "\tsub %s, %lld\n", r0, (int64_t)ins->imm);
				}
				break;
			} else {
				fprintf(f, "\tlea %s, [%s - %lld]\n", r0, r1,
						(int64_t)ins->imm);
				break;
			}
		case IR_INST_MULI:
		case IR_INST_DIVI:
			ERROR("impossible instruction encountered");
			break;
		case IR_INST_EQI:
		case IR_INST_NEI:
		case IR_INST_LTI:
		case IR_INST_LEI:
		case IR_INST_GTI:
		case IR_INST_GEI:
			fprintf(f, "\tcmp %s, %lld\n", r1, (int64_t)ins->imm);
			goto cmp_main;
		case IR_INST_EQ:
		case IR_INST_NE:
		case IR_INST_LT:
		case IR_INST_LE:
		case IR_INST_GT:
		case IR_INST_GE:
			fprintf(f, "\tcmp %s, %s\n", r1, r2);

cmp_main:
			switch(ins->type) {
			case IR_INST_EQ:
			case IR_INST_EQI:
				fprintf(f, "\tsete al\n");
				break;
			case IR_INST_NE:
			case IR_INST_NEI:
				fprintf(f, "\tsetne al\n");
				break;
			case IR_INST_LT:
			case IR_INST_LTI:
				fprintf(f, "\tsetl al\n");
				break;
			case IR_INST_LE:
			case IR_INST_LEI:
				fprintf(f, "\tsetle al\n");
				break;
			case IR_INST_GT:
			case IR_INST_GTI:
				fprintf(f, "\tsetg al\n");
				break;
			case IR_INST_GE:
			case IR_INST_GEI:
				fprintf(f, "\tsetge al\n");
				break;
			default: /* wth? */
				break;
			}
			fprintf(f, "\tmovzx %s, al\n", r0);
			break;
		case IR_INST_LEAS:
			fprintf(f, "\tlea %s, [rbp - %lld]\n", r0, (int64_t)ins->imm);
			break;
		case IR_INST_LOAD: {
			if(!ins->sext) {
				switch(ins->size) {
				case 8:
					fprintf(f, "\tmov %s, [%s]\n", r0, r1);
					break;
				case 4:
					fprintf(f, "\tmov %s, dword ptr [%s]\n", r0d, r1);
					break;
				case 2:
					fprintf(f, "\tmovzx %s, word ptr [%s]\n", r0d, r1);
					break;
				case 1:
					fprintf(f, "\tmovzx %s, byte ptr [%s]\n", r0d, r1);
					break;
				}
			} else {
				switch(ins->size) {
				case 8:
					fprintf(f, "\tmov %s, [%s]\n", r0, r1);
					break;
				case 4:
					fprintf(f, "\tmovsxd %s, dword ptr [%s]\n", r0, r1);
					break;
				case 2:
					fprintf(f, "\tmovsx %s, word ptr [%s]\n", r0, r1);
					break;
				case 1:
					fprintf(f, "\tmovsx %s, byte ptr [%s]\n", r0, r1);
					break;
				}
			}
		} break;
		case IR_INST_LOADS:
		case IR_INST_LOADSS:
			ERROR("todo x64");
			fprintf(f, "\tmov %s, [rbp - %lld]\n", r0,
					i64abs((int64_t)ins->imm));
			break;
		case IR_INST_STORE:
			ERROR("todo x64");
			fprintf(f, "\tmov [%s], %s\n", r1, r2);
			break;
		case IR_INST_STORES:
		case IR_INST_STORESS:
			ERROR("todo x64");
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

static void ir_func_save_regs(FILE *f, ir_func_t *fun)
{
	/* luckily x86_64 is CISC, ez */
	for(size_t i = 0; i < x64_reg_count; i++) {
		if(fun->alloc_used[i]) {
			fprintf(f, "\tpush %s\n", x64_reg[i]);
		}
	}
	return;
}

static void ir_func_restore_regs(FILE *f, ir_func_t *fun)
{
	for(size_t i = x64_reg_count - 1; i >= 0; i--) {
		if(fun->alloc_used[i]) {
			fprintf(f, "\tpop %s\n", x64_reg[i]);
		}

		if(i == 0) {
			break;
		}
	}
	return;
}

void ir_func_emit_x64_sysv(FILE *f, ir_func_t *fun)
{
	char *name = fun->name;
	/* correct syntax */
	fprintf(f, ".intel_syntax noprefix\n");
	fprintf(f, ".global %s\n", name);
	fprintf(f, ".align 4\n");
	fprintf(f, "%s:\n", name);

	/* enter stack frame */
	size_t alignd = align_to(fun->stack_needed, 16);
	fprintf(f, "\tpush rbp\n");
	ir_func_save_regs(f, fun);
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
	ir_func_restore_regs(f, fun);
	fprintf(f, "\tpop rbp\n");
	fprintf(f, "\tret\n");

	return;
}
