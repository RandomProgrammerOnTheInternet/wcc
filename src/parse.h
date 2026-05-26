#ifndef PARSE_H_
#define PARSE_H_

#include "base.h"
#include "lex.h"

/* parser */

/* current grammar:

prim = "(" expr ")" | ident | num
unary = ("+" | "-") unary
		| primary
mul = unary ("*" unary | "/" unary)*
add = mul ("+" mul | "-" mul)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
equality = relational ("==" relational | "!=" relational)*
assign = equality ("=" assign)?
expr = assign
expr-stmt = expr? ";"
stmt = "return" expr ";"
      | "if" "(" expr ")" stmt ("else" stmt)?
      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
      | "while" "(" expr ")" stmt
	  | "{" compound-stmt
	  | expr-stmt
compound-stmt = stmt* "}"
prog = stmt*

*/

enum node_kind {
	NODE_ADD, /* addition + */
	NODE_SUB, /* subtraction - */
	NODE_MUL, /* multiplication * */
	NODE_DIV, /* division / */
	NODE_NUM, /* numbers 123456 */
	NODE_NEG, /* negation - */
	NODE_ASSIGN, /* assignment = */
	NODE_VAR, /* variable ident */
	NODE_EQ, /* equal == */
	NODE_NE, /* not equal != */
	// NODE_GT,   do we need these?
	// NODE_GE,
	NODE_LT, /* less than < */
	NODE_LE, /* less than or equal to <= */
	NODE_EXPR_STMT, /* expression statement */
	NODE_RET, /* return stmt */
	NODE_BLOCK, /* block stmt */
	NODE_IF, /* if */
	NODE_WHILE, /* while */
	NODE_FOR, /* for */
};

/* a variable */
typedef struct obj {
	struct obj *next; /* linked list */
	long off; /* place on stack frame */
	char *name; /* name of variable */
} obj_t;

/* an AST node */
typedef struct node {
	enum node_kind kind;
	/* left-, right-hand side of the tree */
	struct node *lhs, *rhs;
	struct node *next; /* next tree */
	struct node *body; /* inner block */

	/* if condition */
	struct node *cond;
	struct node *then;
	struct node *elze;

	/* for */
	struct node *init;
	struct node *inc;

	obj_t *var; /* for NODE_VAR */
	uint64_t num; /* for NODE_NUM */
} node_t;

/* a function */
typedef struct func {
	node_t *body; /* body of the function */
	obj_t *vars; /* variables of the function */
	size_t stack_size; /* total size of this function's stack frame */
} func_t;

/* makes a node */
node_t *node_make(enum node_kind kind);

/* deallocates a node */
void node_delete(node_t *node);

/* deallocates a whole node AST tree */
void node_delete_all(node_t *root);

/* -- node types -- */

/* make a binop node */
node_t *node_bin(enum node_kind kind, node_t *lhs, node_t *rhs);

/* make a unaryop node */
node_t *node_unary(enum node_kind kind, node_t *lhs);

/* make a number node */
node_t *node_num(uint64_t val);

/* make a variable node */
node_t *node_var(obj_t *var);

/* -- variables/objects -- */

/* create an object with a name `name` */
obj_t *obj_make(char *name);

/* delete an object */
void obj_delete(obj_t *obj);

/* deletes all objects in linked list */
void obj_delete_all(obj_t *root);

/* does the parsing */
func_t *parse_do(token_t *toks);

#endif /* PARSE_H_ */
