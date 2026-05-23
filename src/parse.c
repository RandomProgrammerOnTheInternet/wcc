#include "parse.h"
#include "lex.h"

/* makes a node */
node_t *node_make(enum node_kind kind)
{
	node_t *node = scr_alloc(sizeof(node_t));
	node->kind = kind;
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
	if(root->lhs) {
		node_delete_all(root->lhs);
	}

	if(root->rhs) {
		node_delete_all(root->rhs);
	}

	node_delete(root);
	return;
}

/* -- node types -- */

/* make a binop node */
node_t *node_bin(enum node_kind kind, node_t *lhs, node_t *rhs)
{
	node_t *node = node_make(kind);
	node->lhs = lhs;
	node->rhs = rhs;
	return node;
}

/* make a unaryop node */
node_t *node_unary(enum node_kind kind, node_t *lhs)
{
	node_t *node = node_make(kind);
	node->lhs = lhs;
	return node;
}

/* make a number node */
node_t *node_num(uint64_t val)
{
	node_t *node = node_make(NODE_NUM);
	node->num = val;
	return node;
}

/* make a variable node */
node_t *node_var(char name)
{
	node_t *node = node_make(NODE_VAR);
	node->var = name;
	return node;
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

static node_t *parse_assign(token_t *tok, token_t **rest)
{
	node_t *node = parse_equality(tok, &tok);

	if(token_eq(tok, "=")) {
		node = node_bin(NODE_ASSIGN, node, parse_assign(tok->next, &tok));
	}

	*rest = tok;
	return node;
}

static node_t *parse_stmt(token_t *tok, token_t **rest)
{
	return parse_expr_stmt(tok, rest);
}

static node_t *parse_expr_stmt(token_t *tok, token_t **rest)
{
	node_t *expr = parse_expr(tok, &tok);
	*rest = token_skip(tok, ";");
	return node_unary(NODE_EXPR_STMT, expr);
}

static node_t *parse_add(token_t *tok, token_t **rest)
{
	node_t *node = parse_mul(tok, &tok);
parse:
	if(token_eq(tok, "+")) {
		node = node_bin(NODE_ADD, node, parse_mul(tok->next, &tok));
		goto parse;
	}

	if(token_eq(tok, "-")) {
		node = node_bin(NODE_SUB, node, parse_mul(tok->next, &tok));
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
		node = node_bin(NODE_LT, node, parse_add(tok->next, &tok));
		goto parse;
	}

	if(token_eq(tok, "<=")) {
		node = node_bin(NODE_LE, node, parse_add(tok->next, &tok));
		goto parse;
	}

	if(token_eq(tok, ">")) {
		node = node_bin(NODE_LT, parse_add(tok->next, &tok), node);
		goto parse;
	}

	if(token_eq(tok, ">=")) {
		node = node_bin(NODE_LE, parse_add(tok->next, &tok), node);
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
		node = node_bin(NODE_EQ, node, parse_relational(tok->next, &tok));
		goto parse;
	}

	if(token_eq(tok, "!=")) {
		node = node_bin(NODE_NE, node, parse_relational(tok->next, &tok));
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
		node = node_bin(NODE_MUL, node, parse_unary(tok->next, &tok));
		goto parse;
	}

	if(token_eq(tok, "/")) {
		node = node_bin(NODE_DIV, node, parse_unary(tok->next, &tok));
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
		node_t *node = node_var(*tok->loc);
		*rest = tok->next;
		return node;
	}

	/* num case */
	if(tok->kind == TOK_NUM) {
		node_t *node = node_num(tok->num);
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
		return node_unary(NODE_NEG, parse_unary(tok->next, rest));
	}

	return parse_prim(tok, rest);
}

/* does the parsing */
node_t *parse_do(token_t *toks)
{
	token_t *tok = toks;

	node_t node = { 0 };
	node_t *cur = &node;
	while(tok->kind != TOK_END) {
		node_t *stmt = parse_stmt(tok, &tok);
		cur->next = stmt;
		cur = stmt;
	}

	return node.next;
}
