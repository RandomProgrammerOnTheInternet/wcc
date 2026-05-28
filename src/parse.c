#include "parse.h"
#include "lex.h"
#include "type.h"

static obj_t *locals = NULL;

/* makes a node */
node_t *node_make(enum node_kind kind, token_t *tok)
{
	node_t *node = zalloc(sizeof(node_t));
	node->kind = kind;
	node->tok = tok;
	return node;
}

/* deallocates a node */
void node_delete(node_t *node)
{
	free(node);
	return;
}

/* deallocates a whole node AST tree */
void node_delete_all(node_t *root)
{
#define del(x)              \
	if(x) {                 \
		node_delete_all(x); \
	}

	del(root->lhs);
	del(root->rhs);
	del(root->next);
	del(root->body);
	del(root->cond);
	del(root->then);
	del(root->elze);
	del(root->init);
	del(root->inc);

#undef del
	node_delete(root);
	return;
}

/* -- node types -- */

/* make a binop node */
node_t *node_bin(enum node_kind kind, node_t *lhs, node_t *rhs, token_t *tok)
{
	node_t *node = node_make(kind, tok);
	node->lhs = lhs;
	node->rhs = rhs;
	return node;
}

/* make a unaryop node */
node_t *node_unary(enum node_kind kind, node_t *lhs, token_t *tok)
{
	node_t *node = node_make(kind, tok);
	node->lhs = lhs;
	return node;
}

/* make a number node */
node_t *node_num(uint64_t val, token_t *tok)
{
	node_t *node = node_make(NODE_NUM, tok);
	node->num = val;
	return node;
}

/* make a variable node */
node_t *node_var(obj_t *var, token_t *tok)
{
	node_t *node = node_make(NODE_VAR, tok);
	node->var = var;
	return node;
}

/* -- variables/objects -- */

/* create an object with a name `name` */
obj_t *obj_make(char *name, bool is_func)
{
	/* also inserts it into locals linked list */
	obj_t *obj = scr_alloc(sizeof(obj_t));
	if(!is_func) {
		obj->next = locals;
		locals = obj;
	}
	obj->is_func = is_func;
	obj->name = name;
	obj->off = 0;
	return obj;
}

/* finds a local variable given token */
static obj_t *find_var(token_t *tok)
{
	/* traverse linked list */
	for(obj_t *obj = locals; obj; obj = obj->next) {
		if(strlen(obj->name) == tok->len && *tok->loc == *obj->name &&
		   strncmp(obj->name, tok->loc, tok->len) == 0) {
			return obj;
		}
	}
	return NULL;
}

/* delete an object */
void obj_delete(obj_t *obj)
{
	free(obj->name);
	// scr_free(obj);
}

/* deletes all objects in linked list */
void obj_delete_all(obj_t *root)
{
	obj_t *nxt = NULL;
	obj_t *cur = root;
	while(cur) {
		nxt = cur->next;
		obj_delete(cur);
		cur = nxt;
	}
	return;
}

/* --- recursive descent parser --- */

/* forward defs */
static node_t *parse_mul(token_t *tok, token_t **rest);
static node_t *parse_expr(token_t *tok, token_t **rest);
static node_t *parse_prim(token_t *tok, token_t **rest);
static node_t *parse_unary(token_t *tok, token_t **rest);
static node_t *parse_relational(token_t *tok, token_t **rest);
static node_t *parse_equality(token_t *tok, token_t **rest);
static node_t *parse_add(token_t *tok, token_t **rest);
static node_t *parse_expr_stmt(token_t *tok, token_t **rest);
static node_t *parse_stmt(token_t *tok, token_t **rest);
static node_t *parse_assign(token_t *tok, token_t **rest);
static node_t *parse_compound_stmt(token_t *tok, token_t **rest);

static node_t *parse_compound_stmt(token_t *tok, token_t **rest)
{
	node_t node = { 0 };
	node_t *cur = &node;
	node_t *blk = node_make(NODE_BLOCK, tok);
	while(!token_eq(tok, "}")) {
		node_t *stmt = parse_stmt(tok, &tok);
		cur->next = stmt;
		cur = stmt;
		type_propagate(cur);
	}
	tok = token_skip(tok, "}");

	blk->body = node.next;
	*rest = tok;
	return blk;
}

static node_t *parse_assign(token_t *tok, token_t **rest)
{
	node_t *node = parse_equality(tok, &tok);

	if(token_eq(tok, "=")) {
		node = node_bin(NODE_ASSIGN, node, parse_assign(tok->next, &tok), tok);
	}

	*rest = tok;
	return node;
}

static node_t *parse_stmt(token_t *tok, token_t **rest)
{
	/* return */
	if(tok->kind == TOK_KEYWORD && token_eq(tok, "return")) {
		node_t *stmt = node_unary(NODE_RET, parse_expr(tok->next, &tok), tok);
		*rest = token_skip(tok, ";");
		return stmt;
	}

	/* if */
	if(tok->kind == TOK_KEYWORD && token_eq(tok, "if")) {
		node_t *iffnod = node_make(NODE_IF, tok);
		tok = token_skip(tok->next, "(");
		node_t *iff = parse_expr(tok, &tok);
		tok = token_skip(tok, ")");
		node_t *then = parse_stmt(tok, &tok);
		node_t *elze = NULL;
		/* else */
		if(tok->kind == TOK_KEYWORD && token_eq(tok, "else")) {
			tok = token_skip(tok, "else");
			elze = parse_stmt(tok, &tok);
		}
		*rest = tok;
		iffnod->cond = iff;
		iffnod->then = then;
		iffnod->elze = elze;
		return iffnod;
	}

	/* for */
	if(tok->kind == TOK_KEYWORD && token_eq(tok, "for")) {
		node_t *fornod = node_make(NODE_FOR, tok);
		tok = token_skip(tok->next, "(");
		node_t *init = parse_expr_stmt(tok, &tok);
		node_t *cond = NULL;
		node_t *inc = NULL;

		if(!token_eq(tok, ";")) {
			cond = parse_expr(tok, &tok);
		}

		tok = token_skip(tok, ";");

		if(!token_eq(tok, ")")) {
			inc = parse_expr(tok, &tok);
		}

		tok = token_skip(tok, ")");

		node_t *then = parse_stmt(tok, &tok);
		*rest = tok;
		fornod->init = init;
		fornod->cond = cond;
		fornod->inc = inc;
		fornod->then = then;
		return fornod;
	}

	/* while */
	if(tok->kind == TOK_KEYWORD && token_eq(tok, "while")) {
		node_t *whilenod = node_make(NODE_WHILE, tok);
		tok = token_skip(tok->next, "(");
		node_t *cond = parse_expr(tok, &tok);
		tok = token_skip(tok, ")");
		node_t *body = parse_stmt(tok, &tok);
		*rest = tok;
		whilenod->cond = cond;
		whilenod->then = body;
		return whilenod;
	}

	/* do while */
	if(tok->kind == TOK_KEYWORD && token_eq(tok, "do")) {
		tok = token_skip(tok, "do");
		node_t *dowhile = node_make(NODE_DOWHILE, tok);
		dowhile->then = parse_stmt(tok, &tok);
		tok = token_skip(tok, "while");
		tok = token_skip(tok, "(");
		dowhile->cond = parse_expr(tok, &tok);
		tok = token_skip(tok, ")");
		tok = token_skip(tok, ";");
		*rest = tok;
		return dowhile;
	}

	/* "{" compound-stmt */
	if(token_eq(tok, "{")) {
		node_t *compound_stmt = parse_compound_stmt(tok->next, &tok);
		*rest = tok;
		return compound_stmt;
	}

	return parse_expr_stmt(tok, rest);
}

static node_t *parse_expr_stmt(token_t *tok, token_t **rest)
{
	if(token_eq(tok, ";")) {
		*rest = tok->next;
		return node_make(NODE_BLOCK, tok);
	}
	node_t *expr = parse_expr(tok, &tok);
	*rest = token_skip(tok, ";");
	return node_unary(NODE_EXPR_STMT, expr, tok);
}

static node_t *parse_add(token_t *tok, token_t **rest)
{
	node_t *node = parse_mul(tok, &tok);
parse:
	if(token_eq(tok, "+")) {
		node = node_bin(NODE_ADD, node, parse_mul(tok->next, &tok), tok);
		goto parse;
	}

	if(token_eq(tok, "-")) {
		node = node_bin(NODE_SUB, node, parse_mul(tok->next, &tok), tok);
		goto parse;
	}

	*rest = tok;
	return node;
}

static node_t *parse_relational(token_t *tok, token_t **rest)
{
	node_t *node = parse_add(tok, &tok);
parse:
	if(token_eq(tok, "<")) {
		node = node_bin(NODE_LT, node, parse_add(tok->next, &tok), tok);
		goto parse;
	}

	if(token_eq(tok, "<=")) {
		node = node_bin(NODE_LE, node, parse_add(tok->next, &tok), tok);
		goto parse;
	}

	if(token_eq(tok, ">")) {
		node = node_bin(NODE_GT, node, parse_add(tok->next, &tok), tok);
		goto parse;
	}

	if(token_eq(tok, ">=")) {
		node = node_bin(NODE_GE, node, parse_add(tok->next, &tok), tok);
		goto parse;
	}

	*rest = tok;
	return node;
}

static node_t *parse_equality(token_t *tok, token_t **rest)
{
	node_t *node = parse_relational(tok, &tok);
parse:
	if(token_eq(tok, "==")) {
		node = node_bin(NODE_EQ, node, parse_relational(tok->next, &tok), tok);
		goto parse;
	}

	if(token_eq(tok, "!=")) {
		node = node_bin(NODE_NE, node, parse_relational(tok->next, &tok), tok);
		goto parse;
	}
	*rest = tok;
	return node;
}

static node_t *parse_mul(token_t *tok, token_t **rest)
{
	node_t *node = parse_unary(tok, rest);
	tok = *rest;

parse:
	if(token_eq(tok, "*")) {
		node = node_bin(NODE_MUL, node, parse_unary(tok->next, &tok), tok);
		goto parse;
	}

	if(token_eq(tok, "/")) {
		node = node_bin(NODE_DIV, node, parse_unary(tok->next, &tok), tok);
		goto parse;
	}

	*rest = tok;
	return node;
}

static node_t *parse_expr(token_t *tok, token_t **rest)
{
	return parse_assign(tok, rest);
}

static node_t *parse_prim(token_t *tok, token_t **rest)
{
	/* ( expr ) case */
	if(token_eq(tok, "(")) {
		node_t *node = parse_expr(tok->next, &tok);
		*rest = token_skip(tok, ")");
		return node;
	}

	/* ident case */
	if(tok->kind == TOK_IDENT) {
		obj_t *obj = find_var(tok);
		if(!obj) {
			obj =
				obj_make(memdup_extra(tok->loc, tok->len, tok->len + 1), false);
		}
		node_t *node = node_var(obj, tok);
		*rest = tok->next;
		return node;
	}

	/* num case */
	if(tok->kind == TOK_NUM) {
		node_t *node = node_num(tok->num, tok);
		*rest = tok->next;
		return node;
	}

	compile_err(tok->loc, "expected a expression");

	return NULL;
}

static node_t *parse_unary(token_t *tok, token_t **rest)
{
	if(token_eq(tok, "+")) {
		node_t *node = parse_unary(tok->next, rest);
		return node;
	}

	if(token_eq(tok, "-")) {
		return node_unary(NODE_NEG, parse_unary(tok->next, rest), tok);
	}

	if(token_eq(tok, "&")) {
		return node_unary(NODE_ADDR, parse_unary(tok->next, rest), tok);
	}

	if(token_eq(tok, "*")) {
		return node_unary(NODE_DEREF, parse_unary(tok->next, rest), tok);
	}

	return parse_prim(tok, rest);
}

/* does the parsing */
obj_t *parse_do(token_t *toks)
{
	token_t *tok = token_skip(toks, "{");

	obj_t *f = obj_make("_main", true);

	f->body = parse_compound_stmt(tok, &tok);
	f->vars = locals;
	f->stack_size = -1;

	return f;
}
