#include "type.h"
#include "arena.h"
#include "parse.h"

type_t REAL_TY_INT =
	(type_t){ .kind = TYPE_INT, .size = 8, .align = 8, .to = NULL };
type_t *TY_INT = &REAL_TY_INT;
type_t REAL_TY_PTR =
	(type_t){ .kind = TYPE_PTR, .size = 8, .align = 8, .to = NULL };
type_t *TY_PTR = &REAL_TY_PTR;

bool type_is_int(type_t *ty)
{
	return ty->kind == TYPE_INT;
}

type_t *type_ptr_to(type_t *ty)
{
	type_t *typtr = scr_alloc(sizeof(type_t));
	memcpy(typtr, TY_PTR, sizeof(type_t));
	typtr->to = ty;
	return typtr;
}

static type_t *type_deref(type_t *ty)
{
	if(ty->kind == TYPE_PTR) {
		return ty->to;
	} else {
		return TY_INT;
	}
}

void type_propagate(node_t *node)
{
	if(!node) {
		return;
	}

	type_propagate(node->lhs);
	type_propagate(node->rhs);
	type_propagate(node->next);
	type_propagate(node->cond);
	type_propagate(node->then);
	type_propagate(node->elze);
	type_propagate(node->init);
	type_propagate(node->inc);

	for(node_t *b = node->body; b; b = b->next) {
		type_propagate(b);
	}

	switch(node->kind) {
	case NODE_EQ:
	case NODE_LE:
	case NODE_NE:
	case NODE_LT:
	case NODE_VAR:
	case NODE_NUM:
		node->typ = TY_INT;
		break;
	case NODE_ADD:
	case NODE_SUB:
	case NODE_MUL:
	case NODE_DIV:
	case NODE_NEG:
	case NODE_ASSIGN:
		node->typ = node->lhs->typ;
		break;
	case NODE_ADDR:
		node->typ = type_ptr_to(node->lhs->typ);
		break;
	case NODE_DEREF:
		node->typ = type_deref(node->lhs->typ);
		break;
	default:
		break;
	}
}
