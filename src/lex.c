#include "lex.h"

/* makes a token */
token_t *token_make(enum token_kind kind, char *start, char *end)
{
	token_t *tok = scr_alloc(sizeof(token_t));
	tok->kind = kind;
	tok->loc = start;
	tok->len = end - start;

	return tok;
}

/* deallocates a single token */
void token_delete(token_t *tok)
{
	free(tok);
	return;
}

/* deallocates the whole token linked list */
void token_delete_all(token_t *root)
{
	token_t *nxt;
	token_t *cur = root;
	while(cur && cur->kind != TOK_END) {
		nxt = cur->next;
		free(cur);
		cur = nxt;
	}
	free(cur);
	return;
}

/* checks if a `tok`'s content is equal to `content` */
int token_eq(token_t *tok, char *content)
{
	return strncmp(tok->loc, content, tok->len) == 0 && content[tok->len] == 0;
}

/* skips `tok` and returns next token if `tok`'s content is equal to `content` */
token_t *token_skip(token_t *tok, char *content)
{
	if(!token_eq(tok, content)) {
		compile_err(tok->loc, "expected '%s'", content);
	}
	return tok->next;
}

/* returns the number in `tok` if the token's kind is a number */
uint64_t token_num(token_t *tok)
{
	if(tok->kind != TOK_NUM) {
		compile_err(tok->loc, "expected a number");
	}
	return tok->num;
}

/* is character a punctuator? */
int islexpunct(int c)
{
	return c == '+' || c == '-' || c == '*' || c == '/' || c == ')' ||
		   c == '(' || c == '>' || c == '<' || c == ';' || c == '=';
}

/* is this character an identifier? */
int isident(int c)
{
	return c >= 'a' && c <= 'z';
}

/* returns the length of a possible punctuator */
static int punct_len(char *p)
{
	if(starts_with(p, "<=") || starts_with(p, ">=") || starts_with(p, "==") ||
	   starts_with(p, "!=")) {
		return 2;
	}

	if(islexpunct(*p)) {
		return 1;
	}

	return 0;
}

/* is this character whitespace? */
int iswhitespace(int c)
{
	return c == '\t' || c == ' ' || c == '\r' || c == '\n';
}

/* does the lexing */
token_t *lex_do(char *prog)
{
	token_t start;
	token_t *tok = &start;

	while(*prog) {
		/* skip over whitespace */
		while(iswhitespace(*prog)) {
			prog++;
		}

		/* tokenize number */
		if(isdigit(*prog)) {
			char *num = prog;
			uint64_t intlit = strtol(prog, &prog, 10);
			token_t *numb = token_make(TOK_NUM, num, prog);
			numb->num = intlit;
			tok->next = numb;
			tok = tok->next;
			continue;
		}

		/* tokenize identifiers */
		if(isident(*prog)) {
			token_t *ident = token_make(TOK_IDENT, prog, prog + 1);
			prog++;
			tok->next = ident;
			tok = tok->next;
			continue;
		}

		/* tokenize puncts */
		int plen = punct_len(prog);
		if(plen) {
			token_t *punct = token_make(TOK_PUNCT, prog, prog + plen);
			prog += plen;
			tok->next = punct;
			tok = tok->next;
			continue;
		}

		compile_err(prog, "unknown expression");
	}

	tok = tok->next = token_make(TOK_END, prog, prog);
	return start.next;
}
