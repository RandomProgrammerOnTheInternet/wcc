#ifndef PARSE_H_
#define PARSE_H_

#include "base.h"
#include "lex.h"
#include "type.h"

/* parser */

/* current grammar:

prim = "(" expr ")" | ident args? | num
funcall = ident "(" (assign ("," assign)*)? ")"
unary = ("+" | "-" | "*" | "&") unary
		| prim
mul = unary ("*" unary | "/" unary)*
add = mul ("+" mul | "-" mul)*
relational = add ("<" add | "<=" add | ">" add | ">=" add)*
equality = relational ("==" relational | "!=" relational)*
assign = equality ("=" assign)?
expr = assign
expr-stmt = expr? ";"
declspec = "long" | "int"
declarator = "*"* ident
initalizer = expr
init-declarator = declarator
				| declarator "=" initalizer
declaration = declspec init-declarator ("," init-declarator)* ";"
stmt = "return" expr ";"
      | "if" "(" expr ")" stmt ("else" stmt)?
      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
      | "for" "(" declaration expr? ";" expr? ")" stmt
      | "while" "(" expr ")" stmt
      | "do" stmt "while" "(" expr ")" ";"
	  | "{" compound-stmt
	  | expr-stmt
compound-stmt = (declaration | stmt)* "}"
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
	NODE_GT, /* greater than > */
	NODE_GE, /* greater than or equal to >= */
	NODE_LT, /* less than < */
	NODE_LE, /* less than or equal to <= */
	NODE_EXPR_STMT, /* expression statement */
	NODE_RET, /* return stmt */
	NODE_BLOCK, /* block stmt */
	NODE_IF, /* if */
	NODE_WHILE, /* while */
	NODE_DOWHILE, /* do-while */
	NODE_FOR, /* for */
	NODE_ADDR, /* & */
	NODE_DEREF, /* * */
	NODE_FUNCALL,
};

/* a variable */

struct node;

typedef struct obj {
	struct obj *next; /* linked list */
	long off; /* place on stack frame */
	char *name; /* name of variable */
	type_t *type; /* type of this var */
	bool addressed; /* is this variable addressed? (used for optimization) */

	bool is_func; /* is this object af function? */
	struct node *body; /* body of the function */
	struct obj *vars; /* variables of the function */
	size_t stack_size; /* total size of this function's stack frame */
} obj_t;

/* an AST node */
typedef struct node {
	enum node_kind kind;
	/* left-, right-hand side of the tree */
	struct node *lhs, *rhs;
	struct node *next; /* next tree */
	struct node *body; /* inner block */
	type_t *type; /* type of this node */

	token_t *tok; /* first token of this node */

	/* if condition */
	struct node *cond;
	struct node *then;
	struct node *elze;

	/* for */
	struct node *init;
	struct node *inc;

	char *fname; /* function name, for NODE_FUNCALL */
	struct node *fargs; /* function arguments */
	obj_t *var; /* for NODE_VAR */
	uint64_t num; /* for NODE_NUM */
} node_t;

/* makes a node */
node_t *node_make(enum node_kind kind, token_t *tok);

/* deallocates a node */
void node_delete(node_t *node);

/* deallocates a whole node AST tree */
void node_delete_all(node_t *root);

/* -- node types -- */

/* make a binop node */
node_t *node_bin(enum node_kind kind, node_t *lhs, node_t *rhs, token_t *tok);

/* make a unaryop node */
node_t *node_unary(enum node_kind kind, node_t *lhs, token_t *tok);

/* make a number node */
node_t *node_num(uint64_t val, token_t *tok);

/* make a variable node */
node_t *node_var(obj_t *var, token_t *tok);

/* -- variables/objects -- */

/* create an object with a name `name` */
obj_t *obj_make(char *name, type_t *type, bool is_func);

/* delete an object */
void obj_delete(obj_t *obj);

/* deletes all objects in linked list */
void obj_delete_all(obj_t *root);

/* does the parsing */
obj_t *parse_do(token_t *toks);

#endif /* PARSE_H_ */
