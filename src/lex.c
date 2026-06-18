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

static char read_hex(char *p, char **rest)
{
	char res = 0;
	for(int i = 0; i < 2; i++) {
		char digit = *p++;
		if(digit >= '0' && digit <= '9') {
			res = (16 * res) + (digit - '0');
		} else if(digit >= 'a' && digit <= 'f') {
			res = (16 * res) + (10 + (digit - 'a'));
		} else if(digit >= 'A' && digit <= 'F') {
			res = (16 * res) + (10 + (digit - 'A'));
		} else
			break;
	}
	*rest = p;
	return res;
}

static char read_octal(char *p, char **rest)
{
	char res = 0;
	for(int i = 0; i < 3; i++) {
		char digit = *p++;
		if(digit >= '0' && digit <= '7') {
			res = (8 * res) + (digit - '0');
		} else
			break;
	}
	*rest = p;
	return res;
}

/* read a single (possibly escaped) character */
static char read_chr(char *prog, char **rest)
{
	char *p = prog;
	if(*p != '\\') {
		*rest = p + 1;
		return *p;
	}

	p++;
	char res = 0;
	switch(*p) {
#define CASE(c, v) \
	case c:        \
		res = (v); \
		p++;       \
		break
		CASE('a', 0x07);
		CASE('b', 0x08);
		CASE('e', 0x1b); /* GNU ext. */
		CASE('f', 0x0c);
		CASE('n', 0x0a);
		CASE('r', 0x0d);
		CASE('t', 0x09);
		CASE('v', 0x0b);
		CASE('\\', 0x5c);
		CASE('\'', 0x27);
		CASE('\"', 0x22);
		CASE('?', 0x3f);
#undef CASE

	/* octal byte */
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		res = read_octal(p, &p);
		break;

	/* hex byte */
	case 'x':
		res = read_hex(p + 1, &p);
		break;

	default:
		compile_err(p, "unknown escape character '%s'", *p);
	}

	*rest = p;
	return res;
}

/* read a string */
static strb_t read_str(char *prog, char **rest)
{
	strb_t str = strb_make(NULL, 0);
	char *p = prog;
	while(*p != '\"') {
		strb_add_chr(&str, read_chr(p, &p));
	}
	*rest = p;
	return str;
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
			char *begin = prog + 1;
			/* this is absolutely not correct but I will fix it later */
			strb_t strb = read_str(begin, &prog);
			char *string = strb_build(&strb);
			if(*prog != '\"') {
				compile_err(prog, "unclosed string");
			}
			token_t *str = token_make(TOK_STR, begin, prog);
			str->str = string;
			str->type = type_arr_to(TY_CHAR, strb.len + 1);
			str->start_line = start_line;
			start_line = 0;
			prog++;
			tok->next = str;
			tok = tok->next;
			continue;
		}

		if(*prog == '\'') {
			char *chr = prog + 1;

			char content = read_chr(chr, &chr);
			char *str = zalloc(2);
			str[0] = content;
			if(*chr != '\'') {
				compile_err(prog, "unclosed character literal");
			}

			token_t *chrlit = token_make(TOK_STR, chr, chr);
			chrlit->str = str;
			chrlit->type = TY_CHAR;
			chrlit->start_line = start_line;
			start_line = 0;
			prog = chr + 1;
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
