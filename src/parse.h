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
expr-stmt = expr ";"
stmt = expr-stmt
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
};

/* an AST node */
typedef struct node {
	enum node_kind kind;
	/* left-, right-hand side of the tree */
	struct node *lhs, *rhs;
	struct node *next; /* next tree */
	char var; /* for NODE_VAR */
	uint64_t num; /* for NODE_NUM */
} node_t;

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
node_t *node_var(char name);

/* does the parsing */
node_t *parse_do(token_t *toks);

#endif /* PARSE_H_ */
