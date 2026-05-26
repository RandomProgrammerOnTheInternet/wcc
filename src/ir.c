#include "ir.h"
#include "ir_regalloc.h"
#include "arena.h"

static long counter(int reset)
{
	static int counter = 1;
	if(reset) {
		counter = 0;
	}
	return counter++;
}

/* reset register counter */
void reg_reset_counter(void)
{
	(void)counter(1);
	return;
}

/* make a (new) register */
reg_t *reg_make(void)
{
	reg_t *reg = scr_alloc(sizeof(reg_t));
	wipe(reg, sizeof(reg_t));

	reg->rr = -1;
	reg->spilld = false;
	reg->def = 0;
	reg->last_use = 0;
	reg->vr = counter(0);

	return reg;
}

/* delete a register */
void reg_delete(reg_t *reg)
{
	(void)reg;
	// free(reg);
	// handled by scratch allocator
	return;
}

/* obtain a zero register */
reg_t *reg_zero(void)
{
	reg_t *reg = scr_alloc(sizeof(reg_t));

	reg->rr = -1;
	reg->spilld = false;
	reg->def = 0;
	reg->last_use = 0;
	reg->vr = 0;

	return reg;
}

/* make an IR instruction */
ir_inst_t *ir_inst_make(enum ins_type type, reg_t *r0, reg_t *r1, reg_t *r2,
						uint64_t imm)
{
	ir_inst_t *ins = zalloc(sizeof(ir_inst_t));
	ins->next = NULL;
	ins->type = type;
	ins->r0 = r0;
	ins->r1 = r1;
	ins->r2 = r2;
	ins->false_blk = NULL;
	ins->true_blk = NULL;
	ins->imm = imm;

	return ins;
}

#define MAKE(ty, r0, r1, r2, imm) \
	return ir_inst_make(IR_INST_##ty, r0, r1, r2, imm)

#define INSNAME(name) ins_##name

#define DEF_INS(name, name2, r0, r1, r2, imm, ...) \
	ir_inst_t *INSNAME(name)(__VA_ARGS__)          \
	{                                              \
		MAKE(name2, r0, r1, r2, imm);              \
	}

DEF_INS(nop, NOP, NULL, NULL, NULL, 0, void);
DEF_INS(mov, MOV, r0, r1, NULL, 0, reg_t *r0, reg_t *r1);
DEF_INS(imm, IMM, r0, NULL, NULL, imm, reg_t *r0, uint64_t imm);
DEF_INS(add, ADD, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(sub, SUB, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(mul, MUL, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(div, DIV, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(eq, EQ, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(ne, NE, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(lt, LT, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(le, LE, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(neg, NEG, r0, r1, NULL, 0, reg_t *r0, reg_t *r1);
DEF_INS(leas, LEAS, r0, NULL, NULL, imm, reg_t *r0, long imm);
DEF_INS(load, LOAD, r0, r1, NULL, 0, reg_t *r0, reg_t *r1);
DEF_INS(loads, LOADS, r0, NULL, NULL, imm, reg_t *r0, long imm);
DEF_INS(store, STORE, NULL, r0, r1, 0, reg_t *r0, reg_t *r1);
DEF_INS(stores, STORES, r0, NULL, NULL, imm, reg_t *r0, long imm);
DEF_INS(ret, RET, NULL, r1, NULL, 0, reg_t *r1);

#undef DEF_INS
#undef INSNAME
#undef MAKE

/* the odd one(s) out */
ir_inst_t *ins_br(reg_t *on, ir_blk_t *falseb, ir_blk_t *trueb)
{
	ir_inst_t *ins = ir_inst_make(IR_INST_BR, NULL, on, NULL, 0);
	ins->false_blk = falseb;
	ins->true_blk = trueb;
	return ins;
}

ir_inst_t *ins_jmp(ir_blk_t *blk)
{
	ir_inst_t *ins = ir_inst_make(IR_INST_JMP, NULL, NULL, NULL, 0);
	ins->true_blk = blk;
	return ins;
}

/* delete an IR instruction */
void ir_inst_delete(ir_inst_t *ins)
{
	free(ins);
	return;
}

/* make an IR block */
ir_blk_t *ir_blk_make(ir_inst_t *insts)
{
	ir_blk_t *blk = zalloc(sizeof(ir_blk_t));

	blk->insts = insts;
	blk->succ[0] = NULL;
	blk->succ[1] = NULL;
	blk->pred = list_make(ir_blk_t *);
	blk->regs_def = list_make(reg_t *);
	blk->regs_in = list_make(reg_t *);
	blk->regs_out = list_make(reg_t *);
	blk->tail = NULL;
	return blk;
}

/* add instruction `inst` to block `blk` */
void ir_blk_add(ir_blk_t *blk, ir_inst_t *inst)
{
	if(!blk->insts) {
		blk->tail = inst;
		blk->insts = inst;
		return;
	}
	blk->tail->next = inst;
	blk->tail = inst;
	return;
}

/* delete an IR block */
void ir_blk_delete(ir_blk_t *blk)
{
	list_delete(blk->pred);
	list_delete(blk->regs_def);
	list_delete(blk->regs_in);
	list_delete(blk->regs_out);
	free(blk);
	return;
}

/* make an IR function */
ir_func_t *ir_func_make(char *name)
{
	ir_func_t *fn = zalloc(sizeof(ir_func_t));

	fn->name = name;
	fn->blocks = list_make(ir_blk_t *);

	return fn;
}

/* delete an IR function (aka all blocks, extras) */
void ir_func_delete(ir_func_t *fun)
{
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		ir_inst_t *nxt = NULL;
		for(ir_inst_t *head = blk->insts; head; head = nxt) {
			nxt = head->next;
			ir_inst_delete(head);
		}
		ir_blk_delete(blk);
	}
	list_delete(fun->blocks);
	free(fun);
	return;
}

/* print IR instruction */
void ir_print_inst(ir_inst_t *ins, int mode)
{
#define out(...)         \
	printf(__VA_ARGS__); \
	break;

	long r0, r1, r2;

	if(mode == 'r') {
		r0 = ins->r0 ? (long)ins->r0->rr : -1;
		r1 = ins->r1 ? (long)ins->r1->rr : -1;
		r2 = ins->r2 ? (long)ins->r2->rr : -1;
	} else if(mode == 'v') {
		r0 = ins->r0 ? (long)ins->r0->vr : -1;
		r1 = ins->r1 ? (long)ins->r1->vr : -1;
		r2 = ins->r2 ? (long)ins->r2->vr : -1;
	}
	// #define r0 (long)ins->r0->rr
	// #define r1 (long)ins->r1->rr
	// #define r2 (long)ins->r2->rr
	uint64_t imm = ins->imm;

	switch(ins->type) {
	case IR_INST_NOP:
		out("nop");
	case IR_INST_MOV:
		out("%%r%ld = %%r%ld", r0, r1);
	case IR_INST_IMM:
		out("%%r%ld = #%llu", r0, imm);
	case IR_INST_ADD:
		out("%%r%ld = add %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_SUB:
		out("%%r%ld = sub %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_MUL:
		out("%%r%ld = mul %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_DIV:
		out("%%r%ld = div %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_NEG:
		out("%%r%ld = neg %%r%ld", r0, r1);
	case IR_INST_EQ:
		out("%%r%ld = cmp.eq %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_NE:
		out("%%r%ld = cmp.ne %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_LT:
		out("%%r%ld = cmp.lt %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_LE:
		out("%%r%ld = cmp.le %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_LOAD:
		out("%%r%ld = load %%r%ld", r0, r1);
	case IR_INST_STORE:
		out("store %%r%ld, %%r%ld", r1, r2);
	case IR_INST_LOADS:
	case IR_INST_LOADSS:
		out("%%r%ld = loads #%ld", r0, (long)imm);
	case IR_INST_STORES:
	case IR_INST_STORESS:
		out("stores %%r%ld, #%ld", r1, (long)imm);
	case IR_INST_BR:
		out("br %%r%ld, BB%ld, BB%ld", r1, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_RET:
		out("ret %%r%ld", r1);
	case IR_INST_LEAS:
		out("%%r%ld = leas #%ld", r0, (long)imm);
	case IR_INST_JMP:
		out("jmp BB%ld", ins->true_blk->num);
	default:
		out("????");
	}

	return;
#undef imm
#undef out
#undef r0
#undef r1
#undef r2
}

/* dump IR */
void ir_dump(ir_func_t *fun, int mode)
{
	printf("func %s stack:%ld {\n", fun->name, fun->stack_needed);
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];
		printf("BB%ld:\n", blk->num);
		for(ir_inst_t *inst = blk->insts; inst; inst = inst->next) {
			putchar('\t');
			ir_print_inst(inst, mode);
			putchar('\n');
		}
	}

	printf("}\n");
	return;
}

static void load_imm_lane(FILE *f, int reg, uint16_t i, uint16_t shift,
						  int *keep)
{
	char *kt = "zk";
	if(i) {
		fprintf(f, "\tmov%c x%d, #%hu", kt[*keep], reg, i);
		if(shift) {
			fprintf(f, ", lsl #%hu", shift);
		}
		fprintf(f, "\n");
		*keep = 1;
	}
	return;
}

/* loads an immediate into register `reg` */
static void load_imm(FILE *f, int reg, uint64_t imm_)
{
	uint64_t imm = imm_;
	int64_t imms = (int64_t)imm;

	if(imms <= 4095 && imms >= -4095) {
		fprintf(f, "\tmov x%d, #%lld\n", reg, imms);
		return;
	}

	int keep = 0;

	load_imm_lane(f, reg, (imm >> 0) & 0xffff, 0, &keep);
	load_imm_lane(f, reg, (imm >> 16) & 0xffff, 16, &keep);
	load_imm_lane(f, reg, (imm >> 32) & 0xffff, 32, &keep);
	load_imm_lane(f, reg, (imm >> 48) & 0xffff, 48, &keep);

	return;
}

/* intentionally limiting amount of registers to 5 here to test
 * the register allocator. */
static int arm_reg[5] = { 8, 9, 10, 11, 12 };
/* todo: some of these are argument registers, have to save them for later */
static const char *x64_reg[5] = { "rdi", "rsi", "rdx", "rcx", "r8" };

static void ir_emit_blk_aarch64_apple(FILE *f, ir_func_t *fn, ir_blk_t *blk,
									  size_t i)
{
	/* todo: smarter basic block placement */
	UNUSED(i);
	fprintf(f, "_BB%ld:\n", blk->num);
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		int r0 = ins->r0 ? arm_reg[ins->r0->rr] : -1;
		int r1 = ins->r1 ? arm_reg[ins->r1->rr] : -1;
		int r2 = ins->r2 ? arm_reg[ins->r2->rr] : -1;
		switch(ins->type) {
		case IR_INST_BR:
			fprintf(f, "\ttst x%d, x%d\n", r1, r1);
			fprintf(f, "\tbeq _BB%ld\n", ins->false_blk->num);
			/* big brain optimization */
			/* fallthrough to true block if in front of us */
			if(ins->true_blk->num == blk->num + 1) {
				break;
			}
			/* else don't */
			fprintf(f, "\tb _BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_JMP:
			/* big brain optimization */
			/* fallthrough if target in front of us */
			if(ins->true_blk->num == blk->num + 1) {
				break;
			}
			/* else just jump */
			fprintf(f, "\tb _BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_RET:
			if(r1 != -1) {
				fprintf(f, "\tmov x0, x%d\n", r1);
			}
			fprintf(f, "\tb %s_ret\n", fn->name);
			break;
		case IR_INST_NOP:
			break;
		case IR_INST_MOV:
			fprintf(f, "\tmov x%d, x%d\n", r0, r1);
			break;
		case IR_INST_IMM:
			load_imm(f, r0, ins->imm);
			break;
		case IR_INST_ADD:
			fprintf(f, "\tadd x%d, x%d, x%d\n", r0, r1, r2);
			break;
		case IR_INST_SUB:
			fprintf(f, "\tsub x%d, x%d, x%d\n", r0, r1, r2);
			break;
		case IR_INST_MUL:
			fprintf(f, "\tmul x%d, x%d, x%d\n", r0, r1, r2);
			break;
		case IR_INST_DIV:
			fprintf(f, "\tsdiv x%d, x%d, x%d\n", r0, r1, r2);
			break;
		case IR_INST_NEG:
			fprintf(f, "\tneg x%d, x%d\n", r0, r1);
			break;
		case IR_INST_EQ:
		case IR_INST_NE:
		case IR_INST_LT:
		case IR_INST_LE:
			fprintf(f, "\tcmp x%d, x%d\n", r1, r2);

			switch(ins->type) {
			case IR_INST_EQ:
				fprintf(f, "\tcset x%d, eq\n", r0);
				break;
			case IR_INST_NE:
				fprintf(f, "\tcset x%d, ne\n", r0);
				break;
			case IR_INST_LT:
				fprintf(f, "\tcset x%d, lt\n", r0);
				break;
			case IR_INST_LE:
				fprintf(f, "\tcset x%d, le\n", r0);
				break;

			default: /* wth? */
				break;
			}
			break;
		case IR_INST_LEAS:
			fprintf(f, "\tsub x%d, fp, #%lld\n", r0, ins->imm);
			break;
		case IR_INST_LOAD:
			fprintf(f, "\tldr x%d, [x%d]\n", r0, r1);
			break;
		case IR_INST_LOADS:
		case IR_INST_LOADSS:
			fprintf(f, "\tldr x%d, [fp, #%lld]\n", r0, (int64_t)ins->imm);
			break;

		case IR_INST_STORE:
			fprintf(f, "\tstr x%d, [x%d]\n", r2, r1);
			break;
		case IR_INST_STORES:
		case IR_INST_STORESS:
			fprintf(f, "\tstr x%d, [fp, #%lld]\n", r1, (int64_t)ins->imm);
			break;

		default:
			break;
		}

		if(ins->type == IR_INST_BR || ins->type == IR_INST_JMP ||
		   ins->type == IR_INST_RET) {
			break;
		}
	}
	return;
}

static void ir_func_emit_aarch64_apple(FILE *f, ir_func_t *fun)
{
	fprintf(f, ".globl %s\n", fun->name);
	fprintf(f, ".p2align 2\n");
	fprintf(f, "%s:\n", fun->name);
	/* enter stack frame */
	size_t alignd = align_to(fun->stack_needed, 16);
	fprintf(f, "\tstp fp, lr, [sp, #-16]!\n");
	fprintf(f, "\tmov fp, sp\n");
	if(alignd) {
		fprintf(f, "\tsub sp, sp, #%zu\n", alignd);
	}

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_emit_blk_aarch64_apple(f, fun, fun->blocks[i], i);
	}

	/* leave stack frame */
	fprintf(f, "%s_ret:\n", fun->name);
	fprintf(f, "\tmov sp, fp\n");
	fprintf(f, "\tldp fp, lr, [sp], #16\n");
	fprintf(f, "\tret\n");

	return;
}

static int64_t i64abs(int64_t v)
{
	if(v < 0) {
		return -v;
	}
	return v;
}

static void ir_emit_blk_x64_sysv(FILE *f, ir_func_t *fn, ir_blk_t *blk,
								 size_t i)
{
	UNUSED(i);
	fprintf(f, ".BB%ld:\n", blk->num);
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		const char *r0 = ins->r0 ? x64_reg[ins->r0->rr] : NULL;
		const char *r1 = ins->r1 ? x64_reg[ins->r1->rr] : NULL;
		const char *r2 = ins->r2 ? x64_reg[ins->r2->rr] : NULL;
		switch(ins->type) {
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
			fprintf(f, "\tjmp %s_ret\n", fn->name);
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
			fprintf(f, "\tadd %s, %s\n", r0, r2);
			break;
		case IR_INST_SUB:
			fprintf(f, "\tsub %s, %s\n", r0, r2);
			break;
		case IR_INST_MUL:
			fprintf(f, "\tmul %s, %s\n", r0, r2);
			break;
		case IR_INST_DIV:
			fprintf(f, "\tdiv %s, %s\n", r0, r2);
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

		if(ins->type == IR_INST_BR || ins->type == IR_INST_JMP ||
		   ins->type == IR_INST_RET) {
			break;
		}
	}
	return;
}

static void ir_func_emit_x64_sysv(FILE *f, ir_func_t *fun)
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
	// fprintf(f, "\tstp fp, lr, [sp, #-16]!\n");
	fprintf(f, "\tpush rbp\n");
	fprintf(f, "\tmov rbp, rsp\n");
	if(alignd) {
		fprintf(f, "\tsub rsp, %zu\n", alignd);
	}

	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_emit_blk_x64_sysv(f, fun, fun->blocks[i], i);
	}

	/* leave stack frame */
	fprintf(f, "%s_ret:\n", name);
	fprintf(f, "\tmov rsp, rbp\n");
	fprintf(f, "\tpop rbp\n");
	fprintf(f, "\tret\n");

	fun->name = og_name;
	return;
}

/* codegen an IR function */
/* assumes it has been finalized */
void ir_func_emit(FILE *f, ir_func_t *fun, enum ir_arch arch)
{
	switch(arch) {
	case IR_ARCH_AARCH64_APPLE:
		ir_func_emit_aarch64_apple(f, fun);
		break;
	case IR_ARCH_X64_SYSV:
		ir_func_emit_x64_sysv(f, fun);
		break;
	default:
		break;
	}
}
