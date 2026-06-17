#include "preproc.h"
#include "lex.h"
#include "zz/base.h"

static token_t *preproc_driver(token_t *toks);

/* does the preprocessing */
token_t *preproc_do(token_t *toks_in)
{
	return preproc_driver(toks_in);
}

static token_t *preproc_handle_directive(token_t **prev, token_t *dir)
{
	token_t *nxt = dir->next;
	if(token_eat(&nxt, "include")) {
		if(nxt->kind != TOK_STR) {
			compile_err(nxt->loc, "expected filename");
		}
		FILE *f = fopen(nxt->str, "r");
		ENSURE(f, "failed to read file '%s'", nxt->str);
		char *src = file_reader(f);
		fclose(f);
		token_t *end;
		token_t *new = lex_do(src, &end);
		(*prev)->next = new;
		end->next = nxt->next;

		token_delete(dir->next->next);
		token_delete(dir->next);
		token_delete(dir);

		return end;
	}

	return dir;
}

static token_t *preproc_driver(token_t *toks_in)
{
	token_t *tmp = token_make(TOK_START, NULL, NULL);
	tmp->next = toks_in;
	token_t *toks = tmp;

	token_t *prev = toks;
	for(token_t *iter = toks->next; iter;) {
		if(iter->start_line && token_eq(iter, "#")) {
			preproc_handle_directive(&prev, iter);
			iter = prev;
		}

		prev = iter;
		iter = iter->next;
	}

	token_t *nxt = tmp->next;
	token_delete(tmp);
	return nxt;
}
