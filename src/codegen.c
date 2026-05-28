#include "codegen.h"
#include "parse.h"
#include <stdlib.h>
#include "ir.h"
#include "ir_regalloc.h"

#define load_imm codegen_load_imm
#define push codegen_push
#define push2 codegen_push2
#define pop codegen_pop
#define pop2 codegen_pop2

static ir_func_t *fun;
static ir_blk_t *outblk;

#define MAKE(ty, r0, r1, r2, imm) ir_inst_make(IR_INST_##ty, r0, r1, r2, imm)

#define INSNAME(name) emit_##name

#define DEF_INS(name, name2, r0, r1, r2, imm, ...)     \
	static UNUSEDA void INSNAME(name)(__VA_ARGS__)     \
	{                                                  \
		ir_inst_t *ins = MAKE(name2, r0, r1, r2, imm); \
		ir_blk_add(outblk, ins);                       \
		return;                                        \
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

#define GEN_BRCMP(c, name)                                              \
	static UNUSEDA void emit_##name(reg_t *r1, reg_t *r2, ir_blk_t *fb, \
									ir_blk_t *tb)                       \
	{                                                                   \
		ir_blk_add(outblk, ins_##name(r1, r2, fb, tb));                 \
		return;                                                         \
	}

GEN_BRCMP(IR_INST_BREQ, breq);
GEN_BRCMP(IR_INST_BRNE, brne);
GEN_BRCMP(IR_INST_BRLT, brlt);
GEN_BRCMP(IR_INST_BRLE, brle);

#undef GEN_BRCMP

/* odd one(s) out */
static void emit_br(reg_t *on, ir_blk_t *trueblk, ir_blk_t *falseblk)
{
	ir_blk_add(outblk, ins_br(on, falseblk, trueblk));
	return;
}

static void emit_jmp(ir_blk_t *blk)
{
	ir_blk_add(outblk, ins_jmp(blk));
	return;
}

static ir_blk_t *emit_blk(void)
{
	ir_blk_t *blk = ir_blk_make(NULL);
	blk->num = list_len(fun->blocks);
	list_append(fun->blocks, blk);
	return blk;
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
void codegen_load_imm(FILE *f, int reg, uint64_t imm_)
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

/* pushes `reg` onto stack */
void codegen_push(FILE *f, int reg)
{
	fprintf(f, "\tstr x%d, [sp, #-16]!\n", reg);
	return;
}

/* pops `reg` off stack */
void codegen_pop(FILE *f, int reg)
{
	fprintf(f, "\tldr x%d, [sp], #16\n", reg);
	return;
}

/* pushes `reg1`, `reg2` onto stack */
void codegen_push2(FILE *f, int reg1, int reg2)
{
	fprintf(f, "\tstp x%d, x%d, [sp, #-16]!\n", reg1, reg2);
	return;
}

/* pops `reg2`, `reg1` off stack */
void codegen_pop2(FILE *f, int reg1, int reg2)
{
	fprintf(f, "\tldp x%d, x%d, [sp], #16\n", reg1, reg2);
	return;
}

/* enters a function stack frame */
void codegen_enter(FILE *f, size_t stack_need)
{
	size_t alignd = align_to(stack_need, 16);
	fprintf(f, "\tstp fp, lr, [sp, #-16]!\n");
	if(alignd) {
		fprintf(f, "\tmov fp, sp\n");
		fprintf(f, "\tsub sp, sp, #%zu\n", alignd);
	} else {
		fprintf(f, "\tmov fp, sp\n");
	}
	return;
}

/* leaves a function stack frame */
void codegen_leave(FILE *f)
{
	fprintf(f, "\tmov sp, fp\n");
	fprintf(f, "\tldp fp, lr, [sp], #16\n");
	return;
}

static reg_t *codegen_expr(node_t *node);

/* calculates address of node `node` -- places it into register `reg` */
static reg_t *calc_addr(node_t *node)
{
	if(node->kind == NODE_VAR) {
		long placement = -node->var->off;
		reg_t *addr = reg_make();
		addr->var = node->var;
		emit_leas(addr, placement);
		return addr;
	}
	if(node->kind == NODE_DEREF) {
		return codegen_expr(node->lhs);
	}

	compile_err_node(node, "cannot calculate address of non-variable");

	return NULL;
}

/* generates code given AST tree */
reg_t *codegen_expr(node_t *node)
{
	if(!node) {
		ERROR("null node passed");
	}

	/* special cases */
	switch(node->kind) {
	case NODE_NUM: {
		reg_t *imm = reg_make();

		emit_imm(imm, node->num);
		return imm;
	}
	case NODE_NEG: {
		reg_t *val = codegen_expr(node->lhs);
		reg_t *neg = reg_make();
		emit_neg(neg, val);
		return neg;
	};
	case NODE_VAR: {
		reg_t *addr = calc_addr(node);
		reg_t *val = reg_make();
		val->var = node->var;
		emit_load(val, addr);
		return val;
	};
	case NODE_ADDR: {
		return calc_addr(node->lhs);
	}
	case NODE_DEREF: {
		reg_t *expr = codegen_expr(node->lhs);
		reg_t *val = reg_make();
		emit_load(val, expr);
		return val;
	}
	case NODE_ASSIGN: {
		reg_t *lval = calc_addr(node->lhs);
		reg_t *rval = codegen_expr(node->rhs);
		emit_store(lval, rval);
		return rval;
	};
	default:
		break;
	}

	reg_t *lhs = codegen_expr(node->lhs);
	reg_t *rhs = codegen_expr(node->rhs);
	reg_t *res = reg_make();

	switch(node->kind) {
	default:
		break;

	case NODE_ADD:
		emit_add(res, lhs, rhs);
		break;
	case NODE_SUB:
		emit_sub(res, lhs, rhs);
		break;
	case NODE_MUL:
		emit_mul(res, lhs, rhs);
		break;
	case NODE_DIV:
		emit_div(res, lhs, rhs);
		break;
	case NODE_VAR:
		res = calc_addr(node);
		break;
	case NODE_ASSIGN:
		break;
	case NODE_EQ:
		emit_eq(res, lhs, rhs);
		break;
	case NODE_NE:
		emit_ne(res, lhs, rhs);
		break;
	case NODE_LE:
		emit_le(res, lhs, rhs);
		break;
	case NODE_LT:
		emit_lt(res, lhs, rhs);
		break;
	}

	return res;
}

void codegen_expr_stmt(node_t *node)
{
	switch(node->kind) {
	case NODE_EXPR_STMT:
		(void)codegen_expr(node->lhs);
		break;
	case NODE_RET: {
		reg_t *retval = codegen_expr(node->lhs);
		emit_ret(retval);
		break;
	}
	case NODE_BLOCK:
		for(node_t *nod = node->body; nod; nod = nod->next) {
			codegen_expr_stmt(nod);
		}
		break;
	case NODE_WHILE: {
		ir_blk_t *condchk = emit_blk();
		ir_blk_t *loop = emit_blk();
		ir_blk_t *resume = emit_blk();

		emit_jmp(condchk);

		outblk = condchk;
		reg_t *cond = codegen_expr(node->cond);
		emit_br(cond, loop, resume);

		outblk = loop;
		codegen_expr_stmt(node->then);
		emit_jmp(condchk);
		outblk = resume;
	}; break;
	case NODE_FOR: {
		/* initalizer */
		codegen_expr_stmt(node->init);

		ir_blk_t *condchk = emit_blk(); /* check if need to go loop or resume */
		ir_blk_t *then = emit_blk();
		ir_blk_t *resume = emit_blk();

		emit_jmp(condchk);

		outblk = condchk;
		reg_t *cond;
		if(node->cond) {
			cond = codegen_expr(node->cond);
		} else {
			cond = reg_make();
			emit_imm(cond, 1);
		}

		emit_br(cond, then, resume);

		outblk = then;
		codegen_expr_stmt(node->then);
		if(node->inc) {
			UNUSED(codegen_expr(node->inc));
		}

		emit_jmp(condchk);
		outblk = resume;

	} break;
	case NODE_IF: {
		reg_t *cond = codegen_expr(node->cond);

		ir_blk_t *then = emit_blk(), *elze;
		ir_blk_t *resume = emit_blk();
		if(node->elze == NULL) {
			elze = resume;
		} else {
			elze = emit_blk();
		}

		emit_br(cond, then, elze);
		outblk = then;
		codegen_expr_stmt(node->then);
		emit_jmp(resume);
		if(node->elze) {
			outblk = elze;
			codegen_expr_stmt(node->elze);
			emit_jmp(resume);
		}

		outblk = resume;

	}; break;
	default:
		compile_err_node(node, "invalid stmt");
		break;
	}

	return;
}

/* calculate stack frame space needed for function `fn` */
static void calc_stack_needed(obj_t *fn)
{
	size_t space = 0;
	long off = -8;
	for(obj_t *obj = fn->vars; obj; obj = obj->next) {
		if(obj->is_func) {
			continue;
		}
		space += 8;
		obj->off = off;
		off -= 8;
	}
	fn->stack_size = space;
	return;
}

/* generates code for a function */
void codegen_func(FILE *f, obj_t *fn, int opt_level, enum ir_arch backend)
{
	ENSURE(fn->is_func, "tried to generate code for a variable");
	ir_func_t *func = ir_func_make("_main");
	fun = func;

	ir_blk_t *blk = ir_blk_make(NULL);
	blk->num = 0;
	outblk = blk;
	list_append(func->blocks, blk);

	calc_stack_needed(fn);
	fun->stack_needed = fn->stack_size;
	codegen_expr_stmt(fn->body);

	// ir_dump(func, 'v');
	// putchar('\n');

	ir_opt(func, opt_level, backend);
	ir_finalize(func, 5);
	// ir_dump(func, 'r');

	ir_func_emit(f, func, backend);

	ir_func_delete(func);
	return;
}
