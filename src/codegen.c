#include "codegen.h"
#include "parse.h"
#include <stdlib.h>

#define load_imm codegen_load_imm
#define push codegen_push
#define push2 codegen_push2
#define pop codegen_pop
#define pop2 codegen_pop2

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

int balance = 0;

/* pushes `reg` onto stack */
void codegen_push(FILE *f, int reg)
{
	fprintf(f, "\tstr x%d, [sp, #-16]!\n", reg);
	balance++;
	return;
}

/* pops `reg` off stack */
void codegen_pop(FILE *f, int reg)
{
	fprintf(f, "\tldr x%d, [sp], #16\n", reg);
	balance--;
	return;
}

/* pushes `reg1`, `reg2` onto stack */
void codegen_push2(FILE *f, int reg1, int reg2)
{
	fprintf(f, "\tstp x%d, x%d, [sp, #-16]!\n", reg1, reg2);
	balance += 2;
	return;
}

/* pops `reg2`, `reg1` off stack */
void codegen_pop2(FILE *f, int reg1, int reg2)
{
	fprintf(f, "\tldp x%d, x%d, [sp], #16\n", reg1, reg2);
	balance -= 2;
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

/* calculates address of node `node` -- places it into register `reg` */
static void calc_addr(FILE *f, node_t *node, int reg)
{
	if(node->kind == NODE_VAR) {
		long id = node->var - 'a';
		long placement = 8 * (id + 1);
		fprintf(f, "\tsub x%d, fp, #%ld\n", reg, placement);
		return;
	}

	ERROR("cannot calculate address of non-variable");

	return;
}

/* generates code given AST tree */
void codegen_expr(FILE *f, node_t *node)
{
	if(!node)
		return;

	/* special cases */
	switch(node->kind) {
	case NODE_NUM:
		load_imm(f, 0, node->num);
		return;
	case NODE_NEG:
		codegen_expr(f, node->lhs);
		fprintf(f, "\tneg x0, x0\n");
		return;
	case NODE_VAR:
		calc_addr(f, node, 0);
		fprintf(f, "\tldr x0, [x0]\n");
		return;
	case NODE_ASSIGN:
		calc_addr(f, node->lhs, 0);
		push(f, 0);
		codegen_expr(f, node->rhs);
		pop(f, 1);
		fprintf(f, "\tstr x0, [x1]\n");
		return;
	default:
		break;
	}

	codegen_expr(f, node->rhs);
	push(f, 0);
	codegen_expr(f, node->lhs);
	pop(f, 1);

	switch(node->kind) {
	default:
		break;

	case NODE_ADD:
		fprintf(f, "\tadd x0, x0, x1\n");
		break;
	case NODE_SUB:
		fprintf(f, "\tsub x0, x0, x1\n");
		break;
	case NODE_MUL:
		fprintf(f, "\tmul x0, x0, x1\n");
		break;
	case NODE_DIV:
		fprintf(f, "\tsdiv x0, x0, x1\n");
		break;
	case NODE_VAR:
		calc_addr(f, node, 0);
		break;
	case NODE_ASSIGN:
		break;
	case NODE_EQ:
	case NODE_NE:
	case NODE_LE:
	case NODE_LT:
		fprintf(f, "\tcmp x0, x1\n");

		switch(node->kind) {
		case NODE_EQ:
			fprintf(f, "\tcset x0, eq\n");
			break;
		case NODE_NE:
			fprintf(f, "\tcset x0, ne\n");
			break;
		case NODE_LE:
			fprintf(f, "\tcset x0, le\n");
			break;
		case NODE_LT:
			fprintf(f, "\tcset x0, lt\n");
			break;
		default: /* wth? */
			break;
		}

		break;
	}

	return;
}

void codegen_expr_stmt(FILE *f, node_t *node)
{
	if(node->kind == NODE_EXPR_STMT) {
		codegen_expr(f, node->lhs);
		return;
	}

	ERROR("invalid stmt");
	return;
}

/* generates code for a program */
void codegen_do(FILE *f, node_t *node)
{
	fprintf(f, ".globl _main\n.p2align 2\n_main:\n");
	codegen_enter(f, 8 * 26);
	for(node_t *cur = node; cur; cur = cur->next) {
		codegen_expr_stmt(f, cur);
		ENSURE(balance == 0, "unbalanced program");
	}
	codegen_leave(f);
	fprintf(f, "\tret\n");
	return;
}
