#include "lex.h"
#include "zz/base.h"
#include "type.h"

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
	if(tok->str) {
		free(tok->str);
	}
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

/* checks if a `tok`'s content is equal to `content`. if so, skips to next token and returns 1 */
int token_eat(token_t **tok, char *content)
{
	if(token_eq(*tok, content)) {
		*tok = (*tok)->next;
		return 1;
	}
	return 0;
}

/* returns the number in `tok` if the token's kind is a number */
uint64_t token_num(token_t *tok)
{
	if(tok->kind != TOK_NUM) {
		compile_err(tok->loc, "expected a number");
	}
	return tok->num;
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
	const char *puncts[] = { "<=", ">=", "==", "!=", "&&", "||",  ">>",
							 "<<", "*=", "/=", "+=", "-=", "<<=", ">>=",
							 "&=", "^=", "|=", "++", "--" };
	size_t puncts_len = sizeof(puncts) / sizeof(puncts[0]);

	for(size_t i = 0; i < puncts_len; i++) {
		if(strncmp(p, puncts[i], strlen(puncts[i])) == 0) {
			return strlen(puncts[i]);
		}
	}

	if(ispunct(*p)) {
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
token_t *lex_do(char *prog, token_t **end)
{
	token_t start;
	token_t *tok = &start;

	int start_line = 1;
	while(*prog) {
		/* skip over whitespace */
		while(iswhitespace(*prog)) {
			if(*prog == '\n' || *prog == '\r') {
				start_line = 1;
			}
			prog++;
		}

		if(*prog == 0) {
			break;
		}

		/* if we encounter a //, it is a single line comment.
		 * go to next newline */
		if(*prog == '/' && *(prog + 1) == '/') {
			start_line = 0;
			while(*prog != '\n') {
				prog++;
			}
			continue;
		}

		/* if we see a /\*, it is a multi-line comment.
		 * skip all characters until we see a */
		if(*prog == '/' && *(prog + 1) == '*') {
			start_line = 0;
			char *end = strstr(prog, "*/");
			if(!end) {
				compile_err(prog, "unclosed multi-line comment");
			}
			prog = end + 2;
			continue;
		}

		/* tokenize number */
		if(isdigit(*prog)) {
			char *num = prog;
			uint64_t intlit = strtol(prog, &prog, 10);
			token_t *numb = token_make(TOK_NUM, num, prog);
			numb->num = intlit;
			numb->start_line = start_line;
			start_line = 0;
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
			ident->start_line = start;
			start_line = 0;
			tok = tok->next;
			continue;
		}

		/* tokenize strings */
		if(*prog == '"') {
			/* this is absolutely not correct but I will fix it later */
			char *end = strstr(prog + 1, "\"");
			if(!end) {
				compile_err(prog, "unclosed string");
			}
			token_t *str = token_make(TOK_STR, prog + 1, end - 1);
			str->str = mystrndup(str->loc, (end - prog) - 1);
			str->type = type_arr_to(TY_CHAR, (end - prog));
			str->start_line = start_line;
			start_line = 0;
			prog = end + 1;
			tok->next = str;
			tok = tok->next;
			continue;
		}

		if(*prog == '\'') {
			char *chr = prog + 1;
			if(chr[1] != '\'') {
				compile_err(prog, "unclosed character literal");
			}

			token_t *chrlit = token_make(TOK_STR, chr, chr);
			chrlit->str = mystrndup(chrlit->loc, 1);
			chrlit->type = TY_CHAR;
			chrlit->start_line = start_line;
			start_line = 0;
			prog += 3;
			tok->next = chrlit;
			tok = tok->next;
			continue;
		}

		/* tokenize puncts */
		int plen = punct_len(prog);
		if(plen) {
			token_t *punct = token_make(TOK_PUNCT, prog, prog + plen);
			prog += plen;
			tok->next = punct;
			punct->start_line = start_line;
			start_line = 0;
			tok = tok->next;
			continue;
		}

		compile_err(prog, "unknown expression '%d'", *prog);
	}

	if(end) {
		*end = tok;
	} else {
		tok = tok->next = token_make(TOK_END, prog, prog);
	}

	return start.next;
}
