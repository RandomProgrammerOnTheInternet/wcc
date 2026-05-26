#ifndef CODEGEN_H_
#define CODEGEN_H_

#include "base.h"
#include "lex.h"
#include "parse.h"
#include "ir.h"

/* loads an immediate into register `reg` */
void codegen_load_imm(FILE *f, int reg, uint64_t imm);

/* pushes `reg` onto stack */
void codegen_push(FILE *f, int reg);

/* pops `reg` off stack */
void codegen_pop(FILE *f, int reg);

/* pushes `reg1`, `reg2` onto stack */
void codegen_push2(FILE *f, int reg1, int reg2);

/* pops `reg2`, `reg1` off stack */
void codegen_pop2(FILE *f, int reg1, int reg2);

/* enters a function stack frame */
void codegen_enter(FILE *f, size_t stack_need);

/* leaves a function stack frame */
void codegen_leave(FILE *f);

/* generates code for a function */
void codegen_func(FILE *f, obj_t *fn, enum ir_arch backend);

#endif /* CODEGEN_H_ */
