#include "ir.h"
#include <stdio.h>
#include <stdlib.h>
#include "base.h"
#include <ctype.h>
#include <stdarg.h>
#include "lex.h"
#include "parse.h"
#include "codegen.h"
#include "arena.h"

char *next_arg(int max, int *argc, char *argv[])
{
	if(*argc >= max) {
		ERROR("not enough args supplied");
	}
	char *arg = argv[*argc];
	(*argc)++;
	return arg;
}

int main(int argc, char *argv[])
{
	if(scr_init()) {
		ERROR("failed to allocate scratch allocator");
		return 1;
	}

	if(argc < 2) {
		fprintf(stderr, "usage: %s <src> [opts..]\n", argv[0]);
		return 1;
	}

	enum ir_arch arch = DEFAULT_BACKEND;

	FILE *emit_to = NULL;

	int argc2 = 2;
	while(argc2 < argc) {
		const char *arg = argv[argc2++];
		if(strcmp(arg, "-o") == 0) {
			arg = next_arg(argc, &argc2, argv);
			if(!emit_to) {
				emit_to = fopen(arg, "w");
				ENSURE(emit_to, "failed to open file '%s'", arg);
			}
			continue;
		}
		if(strcmp(arg, "-t") == 0) {
			arg = next_arg(argc, &argc2, argv);
			if(strcmp(arg, "aarch64-apple") == 0) {
				arch = IR_ARCH_AARCH64_APPLE;
			}
			if(strcmp(arg, "x64-sysv") == 0) {
				arch = IR_ARCH_X64_SYSV;
			}
			continue;
		}
		ERROR("unknown argument '%s'", arg);
	}

	if(!emit_to) {
		emit_to = stdout;
	}

	char *prog = strdup(argv[1]);
	compile_setsrc(prog, NULL);

	token_t *head = lex_do(prog);
	token_t *cur = head;
	func_t *prog_node = parse_do(cur);
	codegen_func(emit_to, prog_node, arch);

	free(prog);

	fclose(emit_to);

	token_delete_all(head);
	node_t *curnode = prog_node->body;
	node_t *nxtnode = NULL;
	while(curnode) {
		nxtnode = curnode->next;
		node_delete_all(curnode);
		curnode = nxtnode;
	}
	obj_delete_all(prog_node->vars);

	scr_cleanup();
	return 0;
}
