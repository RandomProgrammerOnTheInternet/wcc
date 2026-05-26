#include "ir_aarch64.h"

void ir_func_opt_aarch64(ir_func_t *fun)
{
	UNUSED(fun);
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

static void ir_emit_blk_aarch64_apple(FILE *f, ir_func_t *fn, ir_blk_t *blk,
									  long last_i)
{
	/* todo: smarter basic block placement */
	fprintf(f, "_BB%ld:\n", blk->num);
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		int r0 = ins->r0 && ins->r0->rr >= 0 ? arm_reg[ins->r0->rr] : -1;
		int r1 = ins->r1 && ins->r1->rr >= 0 ? arm_reg[ins->r1->rr] : -1;
		int r2 = ins->r2 && ins->r2->rr >= 0 ? arm_reg[ins->r2->rr] : -1;
		switch(ins->type) {
		case IR_INST_BREQ:
		case IR_INST_BRNE:
		case IR_INST_BRLT:
		case IR_INST_BRLE:
			fprintf(f, "\tcmp x%d, x%d\n", r1, r2);
			switch(ins->type) {
			default:
				break;
			/* remember: this is for false condition
			 * so invert the specified condition */
			case IR_INST_BREQ:
				fprintf(f, "\tbne _BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRNE:
				fprintf(f, "\tbeq _BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLT:
				fprintf(f, "\tbge _BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLE:
				fprintf(f, "\tbgt _BB%ld\n", ins->false_blk->num);
				break;
			}
			if(ins->true_blk->num == blk->num + 1) {
				/* fallthrough */
				break;
			}
			/* dang it */
			fprintf(f, "\tb _BB%ld\n", ins->true_blk->num);
			break;
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
			if(blk->num != last_i) {
				fprintf(f, "\tb %s_ret\n", fn->name);
			}
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

		if(ir_inst_is_term(ins->type)) {
			break;
		}
	}
	return;
}

void ir_func_emit_aarch64_apple(FILE *f, ir_func_t *fun)
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

	size_t len = list_len(fun->blocks);
	for(size_t i = 0; i < len; i++) {
		ir_emit_blk_aarch64_apple(f, fun, fun->blocks[i], len - 1);
	}

	/* leave stack frame */
	fprintf(f, "%s_ret:\n", fun->name);
	fprintf(f, "\tmov sp, fp\n");
	fprintf(f, "\tldp fp, lr, [sp], #16\n");
	fprintf(f, "\tret\n");

	return;
}
