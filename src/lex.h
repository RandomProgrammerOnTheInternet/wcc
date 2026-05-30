#ifndef LEX_H_
#define LEX_H_

#include "zz/base.h"
#include "zz/list.h"
#include "zz/arena.h"
#include <ctype.h>

/* -- token -- */

enum token_kind {
	TOK_KEYWORD, /* keywords */
	TOK_PUNCT, /* punctuators + - * / */
	TOK_IDENT, /* identifiers az */
	TOK_NUM, /* numbers 1234 */
	TOK_END, /* end */
};

/* a lexed token */
typedef struct token {
	enum token_kind kind;
	struct token *next; /* linked list fun */
	uint64_t num; /* for TOK_NUM */
	char *loc; /* location of token in program */
	size_t len;
} token_t;

/* makes a token */
token_t *token_make(enum token_kind kind, char *start, char *end);

/* deallocates a single token */
void token_delete(token_t *tok);

/* deallocates the whole token linked list */
void token_delete_all(token_t *root);

/* checks if a `tok`'s content is equal to `content` */
int token_eq(token_t *tok, char *content);

/* skips `tok` and returns next token if `tok`'s content is equal to `content` */
token_t *token_skip(token_t *tok, char *content);

/* returns the number in `tok` if the token's kind is a number */
uint64_t token_num(token_t *tok);

/* -- other stuff -- */

/* is this character whitespace? */
int iswhitespace(int c);

/* is this character a punctuator? */
int islexpunct(int c);

/* is this (first) character an identifier? */
int isidentfirst(int c);

/* is this (other) character an identifier? */
int isident(int c);

/* does the lexing */
token_t *lex_do(char *prog);

#endif /* LEX_H_ */
