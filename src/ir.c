#include "ir.h"
#include "ir_regalloc.h"
#include "ir_aarch64.h"
#include "ir_x64.h"
#include "arena.h"

static long counter(int reset)
{
	static int counter = 1;
	if(reset) {
		counter = 0;
	}
	return counter++;
}

/* does this instruction have r2 as an immediate? */
int ir_inst_r2_imm(enum ins_type type)
{
	return type == IR_INST_ADDI || type == IR_INST_SUBI ||
		   type == IR_INST_MULI || type == IR_INST_DIVI ||
		   type == IR_INST_EQI || type == IR_INST_NEI || type == IR_INST_LTI ||
		   type == IR_INST_LEI || type == IR_INST_GTI || type == IR_INST_GTI;
}

int ir_inst_is_term(enum ins_type type)
{
	return type == IR_INST_BR || type == IR_INST_RET || type == IR_INST_JMP ||
		   type == IR_INST_BREQ || type == IR_INST_BRNE ||
		   type == IR_INST_BRLT || type == IR_INST_BRLE ||
		   type == IR_INST_BRGT || type == IR_INST_BRGE ||
		   type == IR_INST_BREQI || type == IR_INST_BRNEI ||
		   type == IR_INST_BRLTI || type == IR_INST_BRLEI ||
		   type == IR_INST_BRGTI || type == IR_INST_BRGEI;
}

/* is this instruction a comparision? */
int ir_inst_is_cmp(enum ins_type type)
{
	return type == IR_INST_EQ || type == IR_INST_NE || type == IR_INST_LE ||
		   type == IR_INST_LT || type == IR_INST_GT || type == IR_INST_GE ||
		   type == IR_INST_EQI || type == IR_INST_NEI || type == IR_INST_LEI ||
		   type == IR_INST_LTI || type == IR_INST_GTI || type == IR_INST_GEI;
}

/* is this instruction foldable? */
static int ir_inst_is_foldable(enum ins_type type)
{
	return ir_inst_is_cmp(type) || type == IR_INST_ADD || type == IR_INST_SUB ||
		   type == IR_INST_MUL || type == IR_INST_DIV || type == IR_INST_ADDI ||
		   type == IR_INST_SUBI || type == IR_INST_MULI || type == IR_INST_DIVI;
}

/* is this instruction associative? (F(B, C) == F(C, B)) */
int ir_inst_is_assoc(enum ins_type type)
{
	return type == IR_INST_EQ || type == IR_INST_NE || type == IR_INST_LT ||
		   type == IR_INST_LE || type == IR_INST_GT || type == IR_INST_GE ||
		   type == IR_INST_ADD || type == IR_INST_MUL;
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
	reg->insty = IR_INST_NOP;
	reg->lhs = reg->rhs = NULL;

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
DEF_INS(gt, GT, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);
DEF_INS(ge, GE, r0, r1, r2, 0, reg_t *r0, reg_t *r1, reg_t *r2);

DEF_INS(addi, ADD, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(subi, SUB, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(muli, MUL, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(divi, DIV, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(eqi, EQ, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(nei, NE, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(lti, LT, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(lei, LE, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(gti, GT, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);
DEF_INS(gei, GE, r0, r1, NULL, imm, reg_t *r0, reg_t *r1, long imm);

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

#define GEN_BRCMP(c, name)                                                  \
	ir_inst_t *ins_##name(reg_t *r1, reg_t *r2, ir_blk_t *fb, ir_blk_t *tb) \
	{                                                                       \
		ir_inst_t *ins = ir_inst_make(c, NULL, r1, r2, 0);                  \
		ins->false_blk = fb;                                                \
		ins->true_blk = tb;                                                 \
		return ins;                                                         \
	}
#define GEN_BRCMPI(c, name)                                                \
	ir_inst_t *ins_##name(reg_t *r1, long imm, ir_blk_t *fb, ir_blk_t *tb) \
	{                                                                      \
		ir_inst_t *ins = ir_inst_make(c, NULL, r1, NULL, imm);             \
		ins->false_blk = fb;                                               \
		ins->true_blk = tb;                                                \
		return ins;                                                        \
	}

GEN_BRCMP(IR_INST_BREQ, breq);
GEN_BRCMP(IR_INST_BRNE, brne);
GEN_BRCMP(IR_INST_BRLT, brlt);
GEN_BRCMP(IR_INST_BRLE, brle);
GEN_BRCMP(IR_INST_BRGT, brgt);
GEN_BRCMP(IR_INST_BRGE, brge);

GEN_BRCMPI(IR_INST_BREQI, breqi);
GEN_BRCMPI(IR_INST_BRNEI, brnei);
GEN_BRCMPI(IR_INST_BRLTI, brlti);
GEN_BRCMPI(IR_INST_BRLEI, brlei);
GEN_BRCMPI(IR_INST_BRGTI, brgti);
GEN_BRCMPI(IR_INST_BRGEI, brgei);

#undef GEN_BRCMP
#undef GEN_BRCMPI

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
	free(fun->alloc_used);
	free(fun);
	return;
}

static void ir_fix_ins(ir_inst_t *ins)
{
	reg_t *r0 = ins->r0;
	reg_t *r1 = ins->r1;
	reg_t *r2 = ins->r2;
#define FIX(t, R0, R1, R2) \
	case IR_INST_##t:      \
		r0 = R0;           \
		r1 = R1;           \
		r2 = R2;           \
		break
#define xx NULL
	switch(ins->type) {
		FIX(NOP, xx, xx, xx);
		FIX(MOV, r0, r1, xx);
		FIX(IMM, r0, xx, xx);
		FIX(NEG, r0, r1, xx);
		FIX(BREQ, xx, r1, r2);
		FIX(BRNE, xx, r1, r2);
		FIX(BRLT, xx, r1, r2);
		FIX(BRLE, xx, r1, r2);
		FIX(BRGT, xx, r1, r2);
		FIX(BRGE, xx, r1, r2);
		FIX(BREQI, xx, r1, xx);
		FIX(BRNEI, xx, r1, xx);
		FIX(BRLTI, xx, r1, xx);
		FIX(BRLEI, xx, r1, xx);
		FIX(BRGTI, xx, r1, xx);
		FIX(BRGEI, xx, r1, xx);
		FIX(ADDI, r0, r1, xx);
		FIX(SUBI, r0, r1, xx);
		FIX(MULI, r0, r1, xx);
		FIX(DIVI, r0, r1, xx);
		FIX(EQI, r0, r1, xx);
		FIX(NEI, r0, r1, xx);
		FIX(LTI, r0, r1, xx);
		FIX(LEI, r0, r1, xx);
		FIX(GTI, r0, r1, xx);
		FIX(GEI, r0, r1, xx);
		FIX(LOAD, r0, r1, xx);
		FIX(STORE, xx, r1, r2);
		FIX(LEAS, r0, xx, xx);
		FIX(LOADS, r0, xx, xx);
		FIX(STORES, xx, r1, xx);
		FIX(LOADSS, r0, xx, xx);
		FIX(STORESS, xx, r1, xx);
		FIX(BR, xx, r1, xx);
		FIX(JMP, xx, xx, xx);
		FIX(RET, xx, r1, xx);
	default:
		break;
	}
#undef FIX
#undef xx

	ins->r0 = r0;
	ins->r1 = r1;
	ins->r2 = r2;
}

/* fixes IR function */
void ir_fix(ir_func_t *func)
{
	for(size_t i = 0; i < list_len(func->blocks); i++) {
		ir_blk_t *blk = func->blocks[i];
		for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
			ir_fix_ins(ins);
		}
	}
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
		out("%%r%ld = #%lld", r0, imm);
	case IR_INST_ADD:
		out("%%r%ld = add %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_SUB:
		out("%%r%ld = sub %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_MUL:
		out("%%r%ld = mul %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_DIV:
		out("%%r%ld = div %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_ADDI:
		out("%%r%ld = addi %%r%ld, #%llu", r0, r1, imm);
	case IR_INST_SUBI:
		out("%%r%ld = subi %%r%ld, #%llu", r0, r1, imm);
	case IR_INST_MULI:
		out("%%r%ld = muli %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_DIVI:
		out("%%r%ld = divi %%r%ld, #%lld", r0, r1, imm);
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
	case IR_INST_GT:
		out("%%r%ld = cmp.gt %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_GE:
		out("%%r%ld = cmp.ge %%r%ld, %%r%ld", r0, r1, r2);
	case IR_INST_EQI:
		out("%%r%ld = cmpi.eq %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_NEI:
		out("%%r%ld = cmpi.ne %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_LTI:
		out("%%r%ld = cmpi.lt %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_LEI:
		out("%%r%ld = cmpi.le %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_GTI:
		out("%%r%ld = cmpi.gt %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_GEI:
		out("%%r%ld = cmpi.ge %%r%ld, #%lld", r0, r1, imm);
	case IR_INST_LOAD:
		out("%%r%ld = load %%r%ld", r0, r1);
	case IR_INST_STORE:
		out("store %%r%ld, %%r%ld", r1, r2);
	case IR_INST_LOADS:
		out("%%r%ld = loads #%ld", r0, (long)imm);
	case IR_INST_LOADSS:
		out("%%r%ld = loadss #%ld", r0, (long)imm);
	case IR_INST_STORES:
		out("stores #%ld, %%r%ld", (long)imm, r1);
	case IR_INST_STORESS:
		out("storess #%ld, %%r%ld", (long)imm, r1);
	case IR_INST_BR:
		out("br %%r%ld, BB%ld, BB%ld", r1, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BREQ:
		out("br.eq %%r%ld, %%r%ld, BB%ld, BB%ld", r1, r2, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRNE:
		out("br.ne %%r%ld, %%r%ld, BB%ld, BB%ld", r1, r2, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRLT:
		out("br.lt %%r%ld, %%r%ld, BB%ld, BB%ld", r1, r2, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRLE:
		out("br.le %%r%ld, %%r%ld, BB%ld, BB%ld", r1, r2, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRGT:
		out("br.gt %%r%ld, %%r%ld, BB%ld, BB%ld", r1, r2, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRGE:
		out("br.ge %%r%ld, %%r%ld, BB%ld, BB%ld", r1, r2, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BREQI:
		out("br.eqi %%r%ld, %lld, BB%ld, BB%ld", r1, imm, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRNEI:
		out("br.nei %%r%ld, %lld, BB%ld, BB%ld", r1, imm, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRLTI:
		out("br.lti %%r%ld, %lld, BB%ld, BB%ld", r1, imm, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRLEI:
		out("br.lei %%r%ld, %lld, BB%ld, BB%ld", r1, imm, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRGTI:
		out("br.gti %%r%ld, %lld, BB%ld, BB%ld", r1, imm, ins->true_blk->num,
			ins->false_blk->num);
	case IR_INST_BRGEI:
		out("br.gei %%r%ld, %lld, BB%ld, BB%ld", r1, imm, ins->true_blk->num,
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
							ins->type = IR_INST_GE;
							break;
						case IR_INST_LE:
							ins->type = IR_INST_GT;
							break;
						case IR_INST_GT:
							ins->type = IR_INST_LE;
							break;
						case IR_INST_GE:
							ins->type = IR_INST_LT;
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

/* removes nops */
void ir_nopremover(ir_func_t *fun)
{
	for(size_t i = 0; i < list_len(fun->blocks); i++) {
		ir_blk_t *blk = fun->blocks[i];

		ir_inst_t *nop = ir_inst_make(IR_INST_NOP, NULL, NULL, NULL, 0);
		nop->next = blk->insts;
		blk->insts = nop;

		ir_inst_t *prev = nop;
		ir_inst_t *nxt = blk->insts->next;
		for(ir_inst_t *ins = blk->insts->next; ins; ins = nxt) {
			nxt = ins->next;

			if(ins->type == IR_INST_NOP) {
				prev->next = nxt;
				ir_inst_delete(ins);
				ins = nxt;
			} else {
				prev = ins;
			}
		}

		blk->insts = blk->insts->next;
		ir_inst_delete(nop);
	}
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

extern int debug;

/* optimizes a function */
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
