#ifndef TYPE_H_
#define TYPE_H_

#include "base.h"

enum type_kind {
	TYPE_INT, /* int (not really for now) */
	TYPE_PTR, /* a pointer */
};

typedef struct type {
	enum type_kind kind;
	size_t size; /* size, alignment */
	size_t align;
	struct type *to; /* a pointer to? */
} type_t;

extern type_t *TY_INT;
extern type_t *TY_PTR;

bool type_is_int(type_t *ty);
bool type_is_ptr(type_t *ty);

type_t *type_ptr_to(type_t *ty);

struct node;
void type_propagate(struct node *node);

#endif /* TYPE_H_ */
