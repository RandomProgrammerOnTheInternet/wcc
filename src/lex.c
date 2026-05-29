#include "lex.h"

/* makes a token */
token_t *token_make(enum token_kind kind, char *start, char *end)
{
	token_t *tok = zalloc(sizeof(token_t));
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
	return tok && content && strncmp(tok->loc, content, tok->len) == 0 &&
		   content[tok->len] == 0;
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
	return ispunct(c);
}

/* is this (other) character an identifier? */
int isident(int c)
{
	return isidentfirst(c) || (c >= '0' && c <= '9');
}

/* is this (first) character an identifier? */
/* 6.4.2.1 nondigit */
int isidentfirst(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
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

/* is this a keyword? */
static int iskeyword(char *prog, size_t plen)
{
	static const char *keywords[] = {
		"auto",		  "break",	   "case",			 "char",
		"const",	  "continue",  "default",		 "do",
		"double",	  "else",	   "enum",			 "extern",
		"float",	  "for",	   "goto",			 "if",
		"inline",	  "int",	   "long",			 "register",
		"restrict",	  "return",	   "short",			 "signed",
		"sizeof",	  "static",	   "struct",		 "switch",
		"typedef",	  "union",	   "unsigned",		 "void",
		"volatile",	  "while",	   "_Alignas",		 "_Alignof",
		"_Atomic",	  "_Bool",	   "_Complex",		 "_Generic",
		"_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
	};
	static const size_t keywords_count = sizeof(keywords) / sizeof(keywords[0]);

	for(size_t i = 0; i < keywords_count; i++) {
		const char *kw = keywords[i];
		size_t len = strlen(kw);
		if(plen != len) {
			continue;
		}
		if(strncmp(prog, kw, len) == 0) {
			return len;
		}
	}

	return 0;
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

		if(*prog == 0) {
			break;
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
		if(isidentfirst(*prog)) {
			char *start = prog;
			while(isident(*prog)) {
				prog++;
			}

			/* tokenize keywords */
			int type = TOK_IDENT;
			if(iskeyword(start, prog - start)) {
				type = TOK_KEYWORD;
			}

			token_t *ident = token_make(type, start, prog);
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

		compile_err(prog, "unknown expression '%d'", *prog);
	}

	tok = tok->next = token_make(TOK_END, prog, prog);
	return start.next;
}
