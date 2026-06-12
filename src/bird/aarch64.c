#include "bird.h"

void ir_func_opt_aarch64(ir_func_t *fun, int opt_level)
{
	UNUSED(opt_level);
	UNUSED(fun);
	return;
}

static void load_imm_lane(FILE *f, const char *reg, uint16_t i, uint16_t shift,
						  int *keep)
{
	char *kt = "zk";
	if(i) {
		fprintf(f, "\tmov%c %s, #%hu", kt[*keep], reg, i);
		if(shift) {
			fprintf(f, ", lsl #%hu", shift);
		}
		fprintf(f, "\n");
		*keep = 1;
	}
	return;
}

/* loads an immediate into register `reg` */
static void load_imm(FILE *f, const char *reg, uint64_t imm_)
{
	uint64_t imm = imm_;
	int64_t imms = (int64_t)imm;

	if(imms <= 4095 && imms >= -4095) {
		fprintf(f, "\tmov %s, #%lld\n", reg, imms);
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

// static int arm_reg[9] = { 19, 20, 21, 22, 23, 24, 25, 26, 27 };
static const char *arm_reg[9] = { "x19", "x20", "x21", "x22", "x23",
								  "x24", "x25", "x26", "x27" };
static const char *arm_reg32[9] = { "w19", "w20", "w21", "w22", "w23",
									"w24", "w25", "w26", "w27" };

static const char *r32[32] = { "w0",  "w1",	 "w2",	"w3",  "w4",  "w5",	 "w6",
							   "w7",  "w8",	 "w9",	"w10", "w11", "w12", "w13",
							   "w14", "w15", "w16", "w17", "w18", "w19", "w20",
							   "w21", "w22", "w23", "w24", "w25", "w26", "w27",
							   "w28", "w29", "w30", "w31" };
static const char *r64[32] = { "x0",  "x1",	 "x2",	"x3",  "x4",  "x5",	 "x6",
							   "x7",  "x8",	 "x9",	"x10", "x11", "x12", "x13",
							   "x14", "x15", "x16", "x17", "x18", "x19", "x20",
							   "x21", "x22", "x23", "x24", "x25", "x26", "x27",
							   "x28", "x29", "x30", "x31" };
#define REG(x, d) ((d) == 4 ? r32[(x)] : r64[(x)])

static const int arm_reg_count = 9;

void ir_prog_begin_aarch64_apple(FILE *f, ir_prog_t *prog)
{
	UNUSED(prog);
	fprintf(f, "\t.p2align 4\n");
}

void ir_prog_end_aarch64_apple(FILE *f, ir_prog_t *prog)
{
	UNUSED(f);
	UNUSED(prog);
	return;
}

void ir_glob_emit_aarch64_apple(FILE *f, ir_global_t *glob)
{
	fprintf(f, "\t.globl _%s\n", glob->name);
	fprintf(f, "\t.data\n");
	if(!glob->has_data) {
		fprintf(f, "\t.zerofill __DATA, __common, _%s, %zu, %zu\n", glob->name,
				glob->size, glob->align);
	} else {
		fprintf(f, "_%s:\n", glob->name);
		for(size_t i = 0; i < glob->size; i++) {
			fprintf(f, "\t.byte %hhu\n", glob->data[i]);
		}
	}
	fprintf(f, "\n");
	return;
}

static INLINE void vload(FILE *f, size_t size, size_t data_size, bool ext,
						 int reg_to, char *addr_fmt, va_list va)
{
	const char *reg = arm_reg[reg_to];
	const char *reg32 = arm_reg32[reg_to];
	const char *prim = data_size == 8 ? reg : reg32;
	switch(size) {
	case 8:
		fprintf(f, "\tldr %s, ", reg);
		if(data_size == 4) {
			fprintf(f, "\tmov %s, %s\n", reg32, reg32);
		}
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 4:
		if(ext) {
			fprintf(f, "\tldrsw %s, ", reg);
		} else {
			fprintf(f, "\tldr %s, ", reg32);
		}
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 2:
		if(ext) {
			fprintf(f, "\tldrsh %s, ", prim);
		} else {
			fprintf(f, "\tldr %s, ", reg32);
		}
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 1:
		if(ext) {
			fprintf(f, "\tldrsb %s,  ", prim);
		} else {
			fprintf(f, "\tldr %s, ", reg32);
		}
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	default:
		break;
	}
	return;
}

static INLINE void vstore(FILE *f, size_t size, size_t data_size, int reg_to,
						  char *addr_fmt, va_list va)
{
	const char *reg = arm_reg[reg_to];
	const char *reg32 = arm_reg32[reg_to];
	switch(size) {
	case 8:
		if(data_size == 4) {
			fprintf(f, "\tmov w28, %s", reg32);
			fprintf(f, "\tstr x28, ");
		} else {
			fprintf(f, "\tstr %s, ", reg);
		}

		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 4:
		fprintf(f, "\tstr %s, ", reg32);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 2:
		fprintf(f, "\tstrh %s, ", reg32);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	case 1:
		fprintf(f, "\tstrb %s, ", reg32);
		vfprintf(f, addr_fmt, va);
		fprintf(f, "\n");
		break;
	default:
		break;
	}
}

static void load(FILE *f, size_t size, size_t data_size, bool ext, int reg_to,
				 char *addr_fmt, ...)
{
	va_list va;
	va_start(va, addr_fmt);
	vload(f, size, data_size, ext, reg_to, addr_fmt, va);
	va_end(va);
	return;
}

static void store(FILE *f, size_t size, size_t data_size, int reg_to,
				  char *addr_fmt, ...)
{
	va_list va;
	va_start(va, addr_fmt);
	vstore(f, size, data_size, reg_to, addr_fmt, va);
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
		const char *arg = arm_reg[reg->rr];
		if(reg->spilld) {
			if(load_fp_imm_x10(f, reg->off, false)) {
				fprintf(f, "\tldr %s, [fp, x10]\n", arg);
			} else {
				fprintf(f, "\tldr %s, [fp, #%ld]\n", arg, reg->off);
			}
		}

		if(i <= 7) {
			fprintf(f, "\tmov x%zu, %s\n", i, arm_reg[reg->rr]);
		} else {
			ENSURE(stack_indx >= 0, "negative stack index, somehow");
			fprintf(f, "\tstr %s, [sp, #%zu]\n", arm_reg[reg->rr], stack_indx);
			stack_indx -= args[i]->size;
		}
	}
	fprintf(f, "\tbl _%s\n", ins->fname);
	if(ins->r0) {
		fprintf(f, "\tmov %s, x0\n", arm_reg[r0]);
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
		const char *r0x = ins->r0 && ins->r0->rr >= 0 ? arm_reg[ins->r0->rr] :
														NULL;
		const char *r1x = ins->r1 && ins->r1->rr >= 0 ? arm_reg[ins->r1->rr] :
														NULL;
		const char *r2x = ins->r2 && ins->r2->rr >= 0 ? arm_reg[ins->r2->rr] :
														NULL;
		const char *r0w = ins->r0 && ins->r0->rr >= 0 ? arm_reg32[ins->r0->rr] :
														NULL;
		const char *r1w = ins->r1 && ins->r1->rr >= 0 ? arm_reg32[ins->r1->rr] :
														NULL;
		const char *r2w = ins->r2 && ins->r2->rr >= 0 ? arm_reg32[ins->r2->rr] :
														NULL;
		const char *r0 = ins->data_size == 8 ? r0x : r0w;
		const char *r1 = ins->data_size == 8 ? r1x : r1w;
		const char *r2 = ins->data_size == 8 ? r2x : r2w;
		int r0i = ins->r0 && ins->r0->rr >= 0 ? ins->r0->rr : -1;
		int r1i = ins->r1 && ins->r1->rr >= 0 ? ins->r1->rr : -1;
		int r2i = ins->r2 && ins->r2->rr >= 0 ? ins->r2->rr : -1;
		size_t sz = ins->size;
		size_t dsz = ins->data_size;
		int64_t imm = (int64_t)ins->imm;
		switch(ins->type) {
		case IR_INST_ZXT:
			switch(ins->size) {
			case 1:
				fprintf(f, "\tuxtb %s, %s\n", r0w, r1w);
				break;
			case 2:
				fprintf(f, "\tuxth %s, %s\n", r0w, r1w);
				break;
			case 4:
				/* this uses w registers don't misread it */
				fprintf(f, "\tmov %s, %s\n", r0w, r1w);
				break;
			case 8:
			default:
				if(r0 != r1) {
					fprintf(f, "\tmov %s, %s\n", r0, r1);
				}
				break;
			}
			break;
		case IR_INST_SXT:
			switch(ins->size) {
			case 1:
				fprintf(f, "\tsxtb %s, %s\n", r0, r1);
				break;
			case 2:
				fprintf(f, "\tsxth %s, %s\n", r0, r1);
				break;
			case 4:
				if(dsz != 4) {
					fprintf(f, "\tsxtw %s, %s\n", r0, r1);
				}
				break;
			case 8:
			default:
				if(r0 != r1) {
					fprintf(f, "\tmov %s, %s\n", r0, r1);
				}
				break;
			}
			break;
		case IR_INST_CALL:
			ir_ins_abi_call_aarch64(f, fn, blk, ins, ins->call_args, r0i, r1i,
									r2i);
			break;
		case IR_INST_BREQ:
		case IR_INST_BRNE:
		case IR_INST_BRSLT:
		case IR_INST_BRSLE:
		case IR_INST_BRSGT:
		case IR_INST_BRSGE:
		case IR_INST_BRULT:
		case IR_INST_BRULE:
		case IR_INST_BRUGT:
		case IR_INST_BRUGE:
			fprintf(f, "\tcmp %s, %s\n", r1, r2);
			switch(ins->type) {
			default:
				break;
			/* remember: this is for false condition
			 * so invert the specified condition */
			case IR_INST_BREQ:
				fprintf(f, "\tbne .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRNE:
				fprintf(f, "\tbeq .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRSLT:
				fprintf(f, "\tbge .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRSLE:
				fprintf(f, "\tbgt .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRSGT:
				fprintf(f, "\tble .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRSGE:
				fprintf(f, "\tblt .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRULT:
				fprintf(f, "\tbhs .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRULE:
				fprintf(f, "\tbhi .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRUGT:
				fprintf(f, "\tbls .BB%ld\n", ins->false_blk->num);
				break;
			case IR_INST_BRUGE:
				fprintf(f, "\tblo .BB%ld\n", ins->false_blk->num);
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
			fprintf(f, "\tcbz %s, .BB%ld\n", r1, ins->false_blk->num);
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
			if(r1i != -1) {
				fprintf(f, "mov %s, %s\n", REG(0, dsz), r1);
			}
			if(blk->num != last_i) {
				fprintf(f, "\tb .L%s_ret\n", fn->name);
			}
			break;
		case IR_INST_NOP:
			break;
		case IR_INST_MOV:
			fprintf(f, "\tmov %s, %s\n", r0, r1);
			break;
		case IR_INST_IMM:
			load_imm(f, r0, ins->imm);
			break;
		case IR_INST_ADD:
			fprintf(f, "\tadd %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_SUB:
			fprintf(f, "\tsub %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_AND:
			fprintf(f, "\tand %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_OR:
			fprintf(f, "\torr %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_EOR:
			fprintf(f, "\teor %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_SHL:
			fprintf(f, "\tlsl %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_SHR:
			fprintf(f, "\tlsr %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_ASHR:
			fprintf(f, "\tasr %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_SMUL:
		case IR_INST_UMUL:
			fprintf(f, "\tmul %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_SDIV:
			fprintf(f, "\tsdiv %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_SMOD:
			fprintf(f, "\tsdiv %s, %s, %s\n", REG(10, dsz), r1, r2);
			fprintf(f, "\tmsub %s, %s, %s, %s\n", r0, REG(10, dsz), r2, r1);
			break;
		case IR_INST_UDIV:
			fprintf(f, "\tudiv %s, %s, %s\n", r0, r1, r2);
			break;
		case IR_INST_UMOD:
			fprintf(f, "\tudiv %s, %s, %s\n", REG(10, dsz), r1, r2);
			fprintf(f, "\tmsub %s, %s, %s, %s\n", r0, REG(10, dsz), r2, r1);
			break;
		case IR_INST_NOT:
			fprintf(f, "\tnot %s, %s\n", r0, r1);
			break;
		case IR_INST_MKBOOL:
			fprintf(f, "\ttst %s, %s\n", r1, r1);
			fprintf(f, "\tcset %s, ne\n", r0);
			break;
		case IR_INST_NOTBOOL:
			fprintf(f, "\ttst %s, %s\n", r1, r1);
			fprintf(f, "\tcset %s, eq\n", r0);
			break;
		case IR_INST_NEG:
			fprintf(f, "\tneg %s, %s\n", r0, r1);
			break;
		case IR_INST_EQ:
		case IR_INST_NE:
		case IR_INST_SLT:
		case IR_INST_SLE:
		case IR_INST_SGT:
		case IR_INST_SGE:
		case IR_INST_ULT:
		case IR_INST_ULE:
		case IR_INST_UGT:
		case IR_INST_UGE:
			fprintf(f, "\tcmp %s, %s\n", r1, r2);
			switch(ins->type) {
			case IR_INST_EQ:
				fprintf(f, "\tcset %s, eq\n", r0);
				break;
			case IR_INST_NE:
				fprintf(f, "\tcset %s, ne\n", r0);
				break;
			case IR_INST_SLT:
				fprintf(f, "\tcset %s, lt\n", r0);
				break;
			case IR_INST_SLE:
				fprintf(f, "\tcset %s, le\n", r0);
				break;
			case IR_INST_SGT:
				fprintf(f, "\tcset %s, gt\n", r0);
				break;
			case IR_INST_SGE:
				fprintf(f, "\tcset %s, ge\n", r0);
				break;
			case IR_INST_ULT:
				fprintf(f, "\tcset %s, lo\n", r0);
				break;
			case IR_INST_ULE:
				fprintf(f, "\tcset %s, ls\n", r0);
				break;
			case IR_INST_UGT:
				fprintf(f, "\tcset %s, hi\n", r0);
				break;
			case IR_INST_UGE:
				fprintf(f, "\tcset %s, hs\n", r0);
				break;
			default: /* wth? */
				break;
			}
			break;
		case IR_INST_LEAS:
			if(load_fp_imm_x10(f, imm, false)) {
				fprintf(f, "\tadd %s, fp, x10\n", r0x);
			} else {
				fprintf(f, "\tsub %s, fp, #%lld\n", r0x, imm);
			}
			break;
		case IR_INST_LEA:
			fprintf(f, "\tadrp %s, _%s@PAGE\n", r0x, ins->label->name);
			fprintf(f, "\tadd %s, %s, _%s@PAGEOFF\n", r0x, r0x,
					ins->label->name);
			break;
		case IR_INST_LOAD:
			load(f, sz, dsz, ins->sign_ext, r0i, "[%s]", r1x);
			break;
		case IR_INST_LOADS:
			if(load_fp_imm_x10(f, imm, false)) {
				load(f, sz, dsz, ins->sign_ext, r0i, "[fp, x10]");
			} else {
				load(f, sz, dsz, ins->sign_ext, r0i, "[fp, #%lld]", imm);
			}
			break;
		case IR_INST_LOADSS:
			if(load_fp_imm_x10(f, imm, false)) {
				load(f, sz, dsz, ins->sign_ext, r0i,
					 "[fp, x10]\t ; spilled load");
			} else {
				load(f, sz, dsz, ins->sign_ext, r0i,
					 "[fp, #%lld]\t ; spilled load", imm);
			}
			break;

		case IR_INST_STORE:
			store(f, sz, dsz, r2i, "[%s]", r1x);
			break;
		case IR_INST_STORES:
			if(load_fp_imm_x10(f, imm, false)) {
				store(f, sz, dsz, r1i, "[fp, x10]");
			} else {
				store(f, sz, dsz, r1i, "[fp, #%lld]", imm);
			}
			break;
		case IR_INST_STORESS:
			if(load_fp_imm_x10(f, imm, false)) {
				store(f, sz, dsz, r1i, "[fp, x10]\t ; spilled store");
			} else {
				store(f, sz, dsz, r1i, "[fp, #%lld]\t ; spilled store", imm);
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
				fprintf(f, "\tstp %s, %s, [sp, #-16]!\n", arm_reg[reg1],
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
			fprintf(f, "\tstr %s, [sp, #-16]!\n", arm_reg[i]);
		}
	}

	free(used_copy);

	return ret;
}

static void ir_func_restore_regs(FILE *f, ir_func_t *fun, int save)
{
	if(save != -1) {
		fprintf(f, "\tldr %s, [sp], #16\n", arm_reg[save]);
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
				fprintf(f, "\tldp %s, %s, [sp], #16\n", arm_reg[reg1],
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
	fprintf(f, "\t.globl _%s\n", fun->name);
	fprintf(f, "\t.text\n");
	fprintf(f, "_%s:\n", fun->name);
	/* enter stack frame */
	size_t alignd = align_to(fun->stack_needed, 16);
	fprintf(f, "\tstp fp, lr, [sp, #-16]!\n");
	int save = ir_func_save_regs(f, fun);

	if(fun->align_needed > 16) {
		fprintf(f, "\tmov x28, sp\n");
		fprintf(f, "\tand sp, x28, #-%zu\n", fun->align_needed);
	}

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
	if(fun->align_needed <= 16) {
		fprintf(f, "\tmov sp, fp\n");
	} else {
		fprintf(f, "\tmov sp, x28\n");
	}
	ir_func_restore_regs(f, fun, save);
	fprintf(f, "\tldp fp, lr, [sp], #16\n");
	fprintf(f, "\tret\n");
	fprintf(f, "\n");

	return;
}
