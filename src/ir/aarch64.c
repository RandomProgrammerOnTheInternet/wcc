#include "ir.h"
#include "aarch64.h"

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

	if(imm < -4095 || imm > 4095) {
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

void ir_func_opt_aarch64(ir_func_t *fun, int opt_level)
{
	UNUSED(opt_level);
	degrade_large_imms(fun);
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

static int load_fp_imm_x10(FILE *f, long off, bool save)
{
	if((-off) >= 65535) {
		ERROR("cannot emit code: stack size larger than 64K");
	}
	if((-off) < 255) {
		return 0;
	}

	if(save) {
		fprintf(f, "\tstr x10, [sp, #-16]!\n");
	}

	fprintf(f, "\tmovn x10, #%llu\n", (uint64_t)(-off));
	return 1;
}

static int arm_reg[10] = { 19, 20, 21, 22, 23, 24, 25, 26, 27, 28 };
static const int arm_reg_count = 10;

static INLINE void vload(FILE *f, size_t size, bool ext, int reg_to,
						 char *addr_fmt, va_list va)
{
	switch(size) {
	case 8:
		fprintf(f, "\tldr x%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 4:
		fprintf(f, ext ? "\tldrsw x%d, " : "\tldr w%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 2:
		fprintf(f, ext ? "\tldrsh x%d, " : "\tldrh w%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 1:
		fprintf(f, ext ? "\tldrsb x%d, " : "\tldrb w%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	default:
		break;
	}
	return;
}

static INLINE void vstore(FILE *f, size_t size, int reg_to, char *addr_fmt,
						  va_list va)
{
	switch(size) {
	case 8:
		fprintf(f, "\tstr x%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 4:
		fprintf(f, "\tstr w%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 2:
		fprintf(f, "\tstrh w%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 1:
		fprintf(f, "\tstrb w%d, ", reg_to);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	default:
		break;
	}
}

static void load(FILE *f, size_t size, bool ext, int reg_to, char *addr_fmt,
				 ...)
{
	va_list va;
	va_start(va, addr_fmt);
	vload(f, size, ext, reg_to, addr_fmt, va);
	va_end(va);
	return;
}

static void store(FILE *f, size_t size, int reg_to, char *addr_fmt, ...)
{
	va_list va;
	va_start(va, addr_fmt);
	vstore(f, size, reg_to, addr_fmt, va);
	va_end(va);
	return;
}

static void ir_ins_abi_call_aarch64(FILE *f, ir_func_t *func, ir_blk_t *blk,
									ir_inst_t *ins, LIST(callreg_t *) args,
									int r0, int r1, int r2)
{
	UNUSED(func);
	UNUSED(blk);
	UNUSED(r1);
	UNUSED(r2);
	size_t alen = list_len(args);
	size_t stack_indx = 0;
	size_t space_needed = 0;
	if(alen > 8) {
		for(size_t i = 8; i < alen; i++) {
			stack_indx = space_needed;
			space_needed += args[i]->size;
		}
		fprintf(f, "\tsub sp, sp, #%zu\n", space_needed);
	}

	for(size_t i = 0; i < alen; i++) {
		reg_t *reg = args[i]->r;
		int arg = arm_reg[reg->rr];
		if(reg->spilld) {
			if(load_fp_imm_x10(f, reg->off, false)) {
				fprintf(f, "\tldr x%d, [fp, x10]\n", arg);
			} else {
				fprintf(f, "\tldr x%d, [fp, #%ld]\n", arg, reg->off);
			}
		}

		if(i <= 7) {
			fprintf(f, "\tmov x%zu, x%d\n", i, arm_reg[reg->rr]);
		} else {
			ENSURE(stack_indx >= 0, "negative stack index, somehow");
			fprintf(f, "\tstr x%d, [sp, #%zu]\n", arm_reg[reg->rr], stack_indx);
			stack_indx -= args[i]->size;
		}
	}
	fprintf(f, "\tbl _%s\n", ins->fname);
	if(ins->r0) {
		fprintf(f, "\tmov x%d, x0\n", r0);
	}
	if(space_needed) {
		fprintf(f, "\tadd sp, sp, #%zu\n", space_needed);
	}

	return;
}

static void ir_emit_blk_aarch64_apple(FILE *f, ir_func_t *fn, ir_blk_t *blk,
									  long last_i)
{
	/* todo: smarter basic block placement */
	fprintf(f, ".BB%ld:\n", blk->num);
	for(ir_inst_t *ins = blk->insts; ins; ins = ins->next) {
		int r0 = ins->r0 && ins->r0->rr >= 0 ? arm_reg[ins->r0->rr] : -1;
		int r1 = ins->r1 && ins->r1->rr >= 0 ? arm_reg[ins->r1->rr] : -1;
		int r2 = ins->r2 && ins->r2->rr >= 0 ? arm_reg[ins->r2->rr] : -1;
		size_t sz = ins->size;
		int64_t imm = (int64_t)ins->imm;
		switch(ins->type) {
		case IR_INST_ZEXT:
			switch(ins->size) {
			case 1:
				fprintf(f, "\tuxtb w%d, w%d\n", r0, r1);
				break;
			case 2:
				fprintf(f, "\tuxth w%d, w%d\n", r0, r1);
				break;
			case 4:
				/* this uses w registers don't misread it */
				fprintf(f, "\tmov w%d, w%d\n", r0, r1);
				break;
			case 8:
			default:
				if(r0 != r1) {
					fprintf(f, "\tmov x%d, x%d\n", r0, r1);
				}
				break;
			}
			break;
		case IR_INST_SEXT:
			switch(ins->size) {
			case 1:
				fprintf(f, "\tsxtb w%d, w%d\n", r0, r1);
				break;
			case 2:
				fprintf(f, "\tsxth w%d, w%d\n", r0, r1);
				break;
			case 4:
				fprintf(f, "\tsxtw x%d, x%d\n", r0, r1);
				break;
			case 8:
			default:
				if(r0 != r1) {
					fprintf(f, "\tmov x%d, x%d\n", r0, r1);
				}
				break;
			}
			break;
		case IR_INST_CALL:
			ir_ins_abi_call_aarch64(f, fn, blk, ins, ins->call_args, r0, r1,
									r2);
			break;
		case IR_INST_BREQI:
		case IR_INST_BRNEI:
		case IR_INST_BRLTI:
		case IR_INST_BRLEI:
		case IR_INST_BRGTI:
		case IR_INST_BRGEI:
			fprintf(f, "\tcmp x%d, #%lld\n", r1, imm);
			goto brcmp_main;
		case IR_INST_BREQ:
		case IR_INST_BRNE:
		case IR_INST_BRLT:
		case IR_INST_BRLE:
		case IR_INST_BRGT:
		case IR_INST_BRGE:
			fprintf(f, "\tcmp x%d, x%d\n", r1, r2);
brcmp_main:
			switch(ins->type) {
			default:
				break;
			/* remember: this is for false condition
			 * so invert the specified condition */
			case IR_INST_BREQ:
			case IR_INST_BREQI:
				fprintf(f, "\tbne .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRNE:
			case IR_INST_BRNEI:
				fprintf(f, "\tbeq .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLT:
			case IR_INST_BRLTI:
				fprintf(f, "\tbge .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRLE:
			case IR_INST_BRLEI:
				fprintf(f, "\tbgt .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRGT:
			case IR_INST_BRGTI:
				fprintf(f, "\tble .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRGE:
			case IR_INST_BRGEI:
				fprintf(f, "\tblt .BB%ld\n", ins->false_blk->num);
				break;
			}
			if(ins->true_blk->num == blk->num + 1) {
				/* fallthrough */
				break;
			}
			/* dang it */
			fprintf(f, "\tb .BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_BR:
			fprintf(f, "\ttst x%d, x%d\n", r1, r1);
			fprintf(f, "\tbeq .BB%ld\n", ins->false_blk->num);
			/* big brain optimization */
			/* fallthrough to true block if in front of us */
			if(ins->true_blk->num == blk->num + 1) {
				break;
			}
			/* else don't */
			fprintf(f, "\tb .BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_JMP:
			/* big brain optimization */
			/* fallthrough if target in front of us */
			if(ins->true_blk->num == blk->num + 1) {
				break;
			}
			/* else just jump */
			fprintf(f, "\tb .BB%ld\n", ins->true_blk->num);
			break;
		case IR_INST_RET:
			if(r1 != -1) {
				fprintf(f, "\tmov x0, x%d\n", r1);
			}
			if(blk->num != last_i) {
				fprintf(f, "\tb .L%s_ret\n", fn->name);
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
		case IR_INST_ADDI:
			fprintf(f, "\tadd x%d, x%d, #%llu\n", r0, r1, ins->imm);
			break;
		case IR_INST_SUBI:
			fprintf(f, "\tsub x%d, x%d, #%llu\n", r0, r1, ins->imm);
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
		case IR_INST_EQI:
		case IR_INST_NEI:
		case IR_INST_LTI:
		case IR_INST_LEI:
		case IR_INST_GTI:
		case IR_INST_GEI:
			fprintf(f, "\tcmp x%d, #%lld\n", r1, imm);
			goto cmp_main;
		case IR_INST_EQ:
		case IR_INST_NE:
		case IR_INST_LT:
		case IR_INST_LE:
		case IR_INST_GT:
		case IR_INST_GE:
			fprintf(f, "\tcmp x%d, x%d\n", r1, r2);

cmp_main:
			switch(ins->type) {
			case IR_INST_EQ:
			case IR_INST_EQI:
				fprintf(f, "\tcset x%d, eq\n", r0);
				break;
			case IR_INST_NE:
			case IR_INST_NEI:
				fprintf(f, "\tcset x%d, ne\n", r0);
				break;
			case IR_INST_LT:
			case IR_INST_LTI:
				fprintf(f, "\tcset x%d, lt\n", r0);
				break;
			case IR_INST_LE:
			case IR_INST_LEI:
				fprintf(f, "\tcset x%d, le\n", r0);
				break;
			case IR_INST_GT:
			case IR_INST_GTI:
				fprintf(f, "\tcset x%d, gt\n", r0);
				break;
			case IR_INST_GE:
			case IR_INST_GEI:
				fprintf(f, "\tcset x%d, ge\n", r0);
				break;
			default: /* wth? */
				break;
			}
			break;
		case IR_INST_LEAS:
			if(load_fp_imm_x10(f, imm, false)) {
				fprintf(f, "\tadd x%d, fp, x10\n", r0);
			} else {
				fprintf(f, "\tsub x%d, fp, #%lld\n", r0, imm);
			}
			break;
		case IR_INST_LOAD:
			load(f, sz, ins->sext, r0, "[x%d]", r1);
			break;
		case IR_INST_LOADS:
			if(load_fp_imm_x10(f, imm, false)) {
				load(f, sz, ins->sext, r0, "[fp, x10]");
			} else {
				load(f, sz, ins->sext, r0, "[fp, #%lld]", imm);
			}
			break;
		case IR_INST_LOADSS:
			if(load_fp_imm_x10(f, imm, false)) {
				load(f, sz, ins->sext, r0, "[fp, x10]\t ; spilled load");
			} else {
				load(f, sz, ins->sext, r0, "[fp, #%lld]\t ; spilled load", imm);
			}
			break;

		case IR_INST_STORE:
			store(f, sz, r2, "[x%d]", r1);
			break;
		case IR_INST_STORES:
			if(load_fp_imm_x10(f, imm, false)) {
				store(f, sz, r1, "[fp, x10]");
			} else {
				store(f, sz, r1, "[fp, #%lld]", imm);
			}
			break;
		case IR_INST_STORESS:
			if(load_fp_imm_x10(f, imm, false)) {
				store(f, sz, r1, "[fp, x10]\t ; spilled store");
			} else {
				store(f, sz, r1, "[fp, #%lld]\t ; spilled store", imm);
			}
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

static int ir_func_save_regs(FILE *f, ir_func_t *fun)
{
	bool *used_copy = zcalloc(arm_reg_count, sizeof(bool));
	memcpy(used_copy, fun->alloc_used, arm_reg_count * sizeof(bool));

	/* save all registers needing to be saved */
	int reg1 = -1;
	int reg2 = -1;
	/* batch up 2 regs */
	for(size_t i = 0; i < arm_reg_count; i++) {
		if(used_copy[i]) {
			if(reg1 == -1) {
				reg1 = (int)i;
			} else if(reg2 == -1) {
				reg2 = (int)i;
				fprintf(f, "\tstp x%d, x%d, [sp, #-16]!\n", arm_reg[reg1],
						arm_reg[reg2]);
				used_copy[reg1] = used_copy[reg2] = 0;
				reg1 = reg2 = -1;
			}
		}
	}

	int ret = -1;

	/* if any remain emit */
	for(size_t i = 0; i < arm_reg_count; i++) {
		if(used_copy[i]) {
			ret = i;
			fprintf(f, "\tstr x%d, [sp, #-16]!\n", arm_reg[i]);
		}
	}

	free(used_copy);

	return ret;
}

static void ir_func_restore_regs(FILE *f, ir_func_t *fun, int save)
{
	if(save != -1) {
		fprintf(f, "\tldr x%d, [sp], #16\n", arm_reg[save]);
		fun->alloc_used[save] = 0;
	}

	/* save all registers needing to be saved */
	int reg1 = -1;
	int reg2 = -1;
	/* batch up 2 regs */
	for(size_t i = arm_reg_count - 1; i >= 0; i--) {
		if(fun->alloc_used[i]) {
			if(reg2 == -1) {
				reg2 = (int)i;
			} else if(reg1 == -1) {
				reg1 = (int)i;
				fprintf(f, "\tldp x%d, x%d, [sp], #16\n", arm_reg[reg1],
						arm_reg[reg2]);
				fun->alloc_used[reg1] = fun->alloc_used[reg2] = 0;
				reg1 = reg2 = -1;
			}
		}

		if(i == 0) {
			break;
		}
	}
	return;
}

void ir_func_emit_aarch64_apple(FILE *f, ir_func_t *fun)
{
	fprintf(f, ".globl _%s\n", fun->name);
	fprintf(f, ".p2align 2\n");
	fprintf(f, "_%s:\n", fun->name);
	/* enter stack frame */
	size_t alignd = align_to(fun->stack_needed, 16);
	fprintf(f, "\tstp fp, lr, [sp, #-16]!\n");
	int save = ir_func_save_regs(f, fun);
	fprintf(f, "\tmov fp, sp\n");

	size_t alen = list_len(fun->args);
	size_t stack_indx = 0;
	size_t space_needed = 0;
	size_t stack_disp = 16;
	for(size_t i = 0; i < arm_reg_count; i++) {
		if(fun->alloc_used[i]) {
			stack_disp += 8;
		}
	}
	if(alen > 8) {
		for(size_t i = 8; i < alen; i++) {
			stack_indx = space_needed;
			space_needed += fun->args[i]->size;
		}
	}

	/* setup function frame */
	for(size_t i = 0; i < list_len(fun->args); i++) {
		callreg_t *arg = fun->args[i];
		if(i < 8) {
			if(load_fp_imm_x10(f, arg->r->off, false)) {
				fprintf(f, "\tstr x%d, [fp, x10]\n", (int)i);
			} else {
				fprintf(f, "\tstr x%d, [fp, #%lld]\n", (int)i,
						(int64_t)arg->r->off);
			}
		} else {
			ENSURE(stack_indx >= 0, "negative stack index, somehow");

			if(load_fp_imm_x10(f, -(int64_t)(stack_indx + stack_disp), false)) {
				fprintf(f, "\tldr x10, [sp, x10]\n");
			} else {
				fprintf(f, "\tldr x10, [sp, #%zu]\n", stack_indx + stack_disp);
			}

			if(load_fp_imm_x10(f, arg->r->off, false)) {
				fprintf(f, "\tstr x10, [fp, x10]\n");
			} else {
				fprintf(f, "\tstr x10, [fp, #%lld]\n", (int64_t)arg->r->off);
			}

			stack_indx -= arg->size;
		}
	}

	if(alignd) {
		fprintf(f, "\tsub sp, sp, #%zu\n", alignd);
	}

	size_t len = list_len(fun->blocks);
	for(size_t i = 0; i < len; i++) {
		ir_emit_blk_aarch64_apple(f, fun, fun->blocks[i],
								  (len - 1) + fun->blocks[0]->num);
	}

	/* leave stack frame */
	fprintf(f, ".L%s_ret:\n", fun->name);
	fprintf(f, "\tmov sp, fp\n");
	ir_func_restore_regs(f, fun, save);
	fprintf(f, "\tldp fp, lr, [sp], #16\n");
	fprintf(f, "\tret\n");

	return;
}
