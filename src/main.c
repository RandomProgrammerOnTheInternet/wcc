#include "ir/ir.h"
#include <stdio.h>
#include <stdlib.h>
#include "zz/base.h"
#include "zz/arena.h"
#include <ctype.h>
#include <stdarg.h>
#include "lex.h"
#include "parse.h"
#include "codegen.h"

int debug = 0;
int opt_level = 0;

char *next_arg(int max, int *argc, char *argv[])
{
	if(*argc >= max) {
		ERROR("not enough args supplied");
	}
	char *arg = argv[*argc];
	(*argc)++;
	return arg;
}

char *file_reader(FILE *f)
{
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *prog = zalloc(size + 2);
	char *crlf_to_lf = zalloc(size + 1);

	long pos = 0;
	while(pos < size) {
		long readback = fread(prog + pos, 1, size - pos, f);
		ENSURE(readback, "failed to read from file");
		pos += readback;
	}

	size_t j = 0;
	/* turn CRLF -> LF because yes */
	for(size_t i = 0; i < (size_t)size; i++) {
		if(prog[i] != '\r' && prog[i] != '\n') {
			crlf_to_lf[j++] = prog[i];
			continue;
		}

		if(prog[i] == '\r' && prog[i + 1] == '\n') {
			crlf_to_lf[j++] = '\n';
			continue;
		}

		if(prog[i] == '\n') {
			crlf_to_lf[j++] = '\n';
			continue;
		}

		ERROR("how did you get here");
	}

	free(prog);

	return crlf_to_lf;
}

static void version(char *pname)
{
	UNUSED(pname);
	printf("wcc version 0.0.1 build %s\n", __DATE__);
	return;
}

static void help(char *pname)
{
	version(pname);
	printf(
		"Usage: %s <input file> [-o <output asm file>] [-t <arch>-<abi>] [-d] [-?/--help]\n",
		pname);
	printf(
		"  -o <output>:\t\tfile to output assembly to (stdout is default)\n");
	printf("  -t <arch>-<abi>:\ttarget architecture, abi\n");
	printf("                  \tonly aarch64-apple, x64-sysv are supported.\n");
	printf("  -d:\t\t\tenable debug IR printing\n");
	printf("  -O0/1/2/3:\t\toptimization level (default: 0)\n");
	printf("  -?, --help:\t\tthis page\n");
	return;
}

int main(int argc, char *argv[])
{
	ENSURE(sizeof(char) == 1 && sizeof(short) == 2 && sizeof(int) == 4,
		   "invalid runtime platform");

	ENSURE(sizeof(long) == sizeof(long long) && sizeof(long) == 8,
		   "Look, I'll add Windows support later, not now.");

	if(scr_init()) {
		ERROR("failed to allocate scratch allocator");
		return 1;
	}

	if(argc == 1) {
		help(argv[0]);
		return 1;
	}

	enum ir_arch arch = DEFAULT_BACKEND;

	FILE *read_from = NULL;
	char *read_from_name = NULL;
	FILE *emit_to = NULL;

	int argc2 = 1;
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

		if(strcmp(arg, "-d") == 0) {
			debug = 1;
			continue;
		}

		if(strcmp(arg, "--help") == 0 || strcmp(arg, "-?") == 0) {
			help(argv[0]);
			return 1;
		}
		if(strstr(arg, ".c")) {
			if(!read_from) {
				read_from = fopen(arg, "r");
				read_from_name = (char *)arg;
				ENSURE(read_from, "failed to open file '%s'", arg);
			} else {
				ENSURE(2 + 2 == 3, "TODO: Multiple file compilation");
			}
			continue;
		}
		if(starts_with((char *)arg, "-O")) {
			char *num = (char *)arg + 2;
			if(num && *num >= '0' && *num <= '3') {
				opt_level = *num - '0';
			} else {
				WARN("unknown optimization level '%s', defaulting to 0", arg);
				opt_level = 0;
			}
			continue;
		}

		ERROR("unknown argument '%s'", arg);
	}

	if(!read_from) {
		ERROR("need an input file");
	}

	if(arch == IR_ARCH_X64_SYSV) {
		// WARN("x64-sysv backend is experimental");
	}

	if(!emit_to) {
		emit_to = stdout;
	}

	char *prog = file_reader(read_from);
	fclose(read_from);
	compile_setsrc(prog, read_from_name);

	token_t *head = lex_do(prog);
	token_t *cur = head;
	obj_t *prog_node = parse_do(cur);
	codegen_func(emit_to, prog_node, opt_level, arch);

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
