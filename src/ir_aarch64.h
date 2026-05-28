#ifndef IR_AARCH64_H_
#define IR_AARCH64_H_

#include "ir.h"

void ir_func_opt_aarch64(ir_func_t *fun, int opt_level);
void ir_func_emit_aarch64_apple(FILE *f, ir_func_t *fun);

#endif /* IR_AARCH64_H_ */
