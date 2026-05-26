/* simple three-address-code IR, might do SSA later */
#ifndef IR_H_
#define IR_H_

#include "base.h"
#include "lex.h"
#include "parse.h"
#include "list.h"

enum ins_type {
	IR_INST_NOP, /* does nothing */

	/* data transfer */
	IR_INST_MOV, /* %r0 = %r1 */
	IR_INST_IMM, /* %r0 = #imm */

	/* arithmetic - binops */
	IR_INST_ADD, /* %r0 = add %r1, %r2 */
	IR_INST_SUB, /* %r0 = sub %r1, %r2 */
	IR_INST_MUL, /* %r0 = mul %r1, %r2 */
	IR_INST_DIV, /* %r0 = div %r1, %r2 */

	/* arithmetic - unaryops */
	IR_INST_NEG, /* %r0 = neg %r1 */

	/* comparisons (all signed for now) */
	IR_INST_EQ, /* %r0 = cmp.eq %r1, %r2 */
	IR_INST_NE, /* %r0 = cmp.ne %r1, %r2 */
	IR_INST_LT, /* %r0 = cmp.lt %r1, %r2 */
	IR_INST_LE, /* %r0 = cmp.le %r1, %r2 */

	/* compare-and-branches (all signed for now) */
	IR_INST_BREQ, /* breq %r1, %r2, true-blk, false-blk */
	IR_INST_BRNE, /* brne %r1, %r2, true-blk, false-blk */
	IR_INST_BRLT, /* brlt %r1, %r2, true-blk, false-blk */
	IR_INST_BRLE, /* brle %r1, %r2, true-blk, false-blk */

	/* memory */
	IR_INST_LOAD, /* %r0 = load %r1 */
	IR_INST_STORE, /* store %r1, %r2 */
	IR_INST_LEAS, /* %r0 = lea %sp, #imm */
	/* load, store from stack */
	IR_INST_LOADS, /* %r0 = loads #imm */
	IR_INST_STORES, /* stores %r1, #imm */
	/* spilled load, store */
	IR_INST_LOADSS, /* %r0 = loadss #imm */
	IR_INST_STORESS, /* stores %r1, #imm */

	/* basic block stuff */
	IR_INST_BR, /* br %r1, false-blk, true-blk */
	IR_INST_JMP, /* jmp blk */
	IR_INST_RET, /* ret (%r1) */
};

/* a "register" */
typedef struct reg {
	long vr; /* virtual register # */
	int rr; /* real register # */

	/* for register allocation: */
	long def; /* when this reg was defined */
	long last_use; /* when this reg was last used */
	bool spilld; /* is this reg spilled? */
	size_t age; /* age of this register */
	//obj_t *var; /* variable of register */
	long off; /* offset of register */
	/* for optimization: */
	bool stack_loc; /* is this register from a leas instruction? */
	long stack_off; /* if so, it's offset */
	/* instruction register comes from */
	enum ins_type insty;
	struct reg *lhs;
	struct reg *rhs;
} reg_t;

enum ir_arch {
	/* architecture-abi */
	IR_ARCH_AARCH64_APPLE, /* aarch64-apple */
	IR_ARCH_X64_SYSV, /* x64-sysv */
};

struct ir_blk;

/* an IR instruction. is a linked list */
typedef struct ir_inst {
	struct ir_inst *next; /* next ins */
	enum ins_type type; /* instruction type */
	reg_t *r0, *r1, *r2; /* instruction args */
	uint64_t imm; /* immediate, if needed */
	struct ir_blk *false_blk, *true_blk; /* for br */
} ir_inst_t;

/* IR block (collection of instructions, >= 1 entry and only <= 2 exits) */
typedef struct ir_blk {
	ir_inst_t *insts; /* instructions in this block */
	ir_inst_t *tail; /* last instruction in block */
	bool returns; /* does this block return? */
	long num; /* this block's # */

	/* register allocation stuff */
	struct ir_blk *succ[2]; /* block's successors */
	LIST(struct ir_blk *) pred; /* block's predecessors */
	LIST(reg_t *) regs_def; /* registers in this block */
	LIST(reg_t *) regs_in; /* registers in */
	LIST(reg_t *) regs_out; /* registers out */
} ir_blk_t;

/* IR function (collection of blocks) */
typedef struct ir_func {
	char *name; /* name of this function */
	LIST(ir_blk_t *) blocks; /* the collection of blocks */
	size_t stack_needed; /* stack space needed for this function */
} ir_func_t;

/* -- big list of instructions -- */
#define INSNAME(name) ins_##name

#define DEF_INS(name, ...) ir_inst_t *INSNAME(name)(__VA_ARGS__)

DEF_INS(nop, void);
DEF_INS(mov, reg_t *r0, reg_t *r1);
DEF_INS(imm, reg_t *r0, uint64_t imm);
DEF_INS(add, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(sub, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(mul, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(div, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(eq, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(ne, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(lt, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(le, reg_t *r0, reg_t *r2, reg_t *r3);
DEF_INS(neg, reg_t *r0, reg_t *r1);
DEF_INS(leas, reg_t *r0, long imm);
DEF_INS(load, reg_t *r0, reg_t *r1);
DEF_INS(loads, reg_t *r0, long imm);
DEF_INS(store, reg_t *r0, reg_t *r1);
DEF_INS(stores, reg_t *r0, long imm);
DEF_INS(br, reg_t *r1, ir_blk_t *falseblk, ir_blk_t *trueblk);
DEF_INS(breq, reg_t *r1, reg_t *r2, ir_blk_t *falseblk, ir_blk_t *trueblk);
DEF_INS(brne, reg_t *r1, reg_t *r2, ir_blk_t *falseblk, ir_blk_t *trueblk);
DEF_INS(brlt, reg_t *r1, reg_t *r2, ir_blk_t *falseblk, ir_blk_t *trueblk);
DEF_INS(brle, reg_t *r1, reg_t *r2, ir_blk_t *falseblk, ir_blk_t *trueblk);
DEF_INS(jmp, ir_blk_t *blk);
DEF_INS(ret, reg_t *r1);

#undef DEF_INS
#undef INSNAME

/* does this instruction terminate a block? */
int ir_inst_is_term(enum ins_type type);

/* is this instruction a comparision? */
int ir_inst_is_cmp(enum ins_type type);

/* make a (new) register */
reg_t *reg_make(void);

/* delete a register */
void reg_delete(reg_t *reg);

/* obtain a zero register */
reg_t *reg_zero(void);

/* reset register counter */
void reg_reset_counter(void);

/* make an IR instruction */
ir_inst_t *ir_inst_make(enum ins_type type, reg_t *r0, reg_t *r1, reg_t *r2,
						uint64_t imm);

/* delete an IR instruction */
void ir_inst_delete(ir_inst_t *ins);

/* make an IR block */
ir_blk_t *ir_blk_make(ir_inst_t *insts);

/* delete an IR block */
void ir_blk_delete(ir_blk_t *blk);

/* make an IR function */
ir_func_t *ir_func_make(char *name);

/* optimizes an IR function */
void ir_opt(ir_func_t *fun);

/* delete an IR function (aka all blocks, extras) */
void ir_func_delete(ir_func_t *fun);

/* print IR instruction */
void ir_print_inst(ir_inst_t *ins, int mode);

/* dump IR */
void ir_dump(ir_func_t *fun, int mode);

/* codegen an IR function */
/* assumes function has been finalized */
void ir_func_emit(FILE *f, ir_func_t *fun, enum ir_arch arch);

/* add IR instruction to IR block */
void ir_blk_add(ir_blk_t *blk, ir_inst_t *inst);

#endif /* IR_H_ */
