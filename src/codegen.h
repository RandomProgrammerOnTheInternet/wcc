#ifndef CODEGEN_H_
#define CODEGEN_H_

#include "zz/base.h"
#include "lex.h"
#include "parse.h"
#include "ir.h"

/* generates code for a function */
void codegen_func(FILE *f, obj_t *fn, int opt_level, enum ir_arch backend);

#endif /* CODEGEN_H_ */
