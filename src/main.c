#include <stdio.h>
#include <stdlib.h>
#include "base.h"
#include <ctype.h>
#include <stdarg.h>
#include "lex.h"
#include "parse.h"
#include "codegen.h"
#include "arena.h"

int main(int argc, char *argv[])
{
	if(scr_init()) {
		ERROR("failed to allocate scratch allocator");
		return 1;
	}

	if(argc < 2) {
		fprintf(stderr, "error\n");
		return 1;
	}

	FILE *emit_to;

	if(argc == 3) {
		emit_to = fopen(argv[2], "w");
		if(!emit_to) {
			fprintf(stderr, "error opening file\n");
			return 1;
		}
	} else {
		emit_to = stdout;
	}

	char *prog = strdup(argv[1]);
	compile_setsrc(prog, NULL);

	token_t *head = lex_do(prog);
	token_t *cur = head;
	node_t *prog_node = parse_do(cur);
	codegen_do(emit_to, prog_node);

	free(prog);

	fclose(emit_to);

	// todo, custom arena allocator support
	// token_delete_all(head);
	// node_delete_all(prog_node);

	scr_cleanup();
	return 0;
}
