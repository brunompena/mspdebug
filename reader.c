/* MSPDebug - debugging tool for MSP430 MCUs
 * Copyright (C) 2009, 2010 Daniel Beer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "vector.h"
#include "util.h"
#include "output.h"
#include "cmddb.h"
#include "stdcmd.h"
#include "reader.h"

static int modify_flags;
static int in_reader_loop;

void mark_modified(int flags)
{
	modify_flags |= flags;
}

void unmark_modified(int flags)
{
	modify_flags &= ~flags;
}

int prompt_abort(int flags)
{
        char buf[32];

        if (!(in_reader_loop && (modify_flags & flags)))
                return 0;

        for (;;) {
                printc("Symbols have not been saved since modification. "
                       "Continue (y/n)? ");
                fflush(stdout);

                if (!fgets(buf, sizeof(buf), stdin)) {
                        printc("\n");
                        return 1;
                }

                if (toupper(buf[0]) == 'Y')
                        return 0;
                if (toupper(buf[0]) == 'N')
                        return 1;

                printc("Please answer \"y\" or \"n\".\n");
        }

        return 0;
}

#ifndef USE_READLINE
#define LINE_BUF_SIZE 128

static char *readline(const char *prompt)
{
	char *buf = malloc(LINE_BUF_SIZE);

	if (!buf) {
		pr_error("readline: can't allocate memory");
		return NULL;
	}

	for (;;) {
		printc("(mspdebug) ");
		fflush(stdout);

		if (fgets(buf, LINE_BUF_SIZE, stdin))
			return buf;

		if (feof(stdin))
			break;

		printc("\n");
	}

	free(buf);
	return NULL;
}

#define add_history(x)
#endif

static int do_command(char *arg, int interactive)
{
	const char *cmd_text;
	int len = strlen(arg);

	while (len && isspace(arg[len - 1]))
		len--;
	arg[len] = 0;

	cmd_text = get_arg(&arg);
	if (cmd_text) {
		struct cmddb_record cmd;

		if (!cmddb_get(cmd_text, &cmd)) {
			int old = in_reader_loop;
			int ret;

			in_reader_loop = interactive;
			ret = cmd.func(&arg);
			in_reader_loop = old;

			return ret;
		}

		printc_err("unknown command: %s (try \"help\")\n",
			cmd_text);
		return -1;
	}

	return 0;
}

void reader_loop(void)
{
	int old = in_reader_loop;

	in_reader_loop = 1;

	printc("\n");
	cmd_help(NULL);
	printc("\n");

	do {
		for (;;) {
			char *buf = readline("(mspdebug) ");

			if (!buf)
				break;

			add_history(buf);
			do_command(buf, 1);
			free(buf);
		}
	} while (prompt_abort(MODIFY_SYMS));

	printc("\n");
	in_reader_loop = old;
}

int process_command(char *cmd)
{
	return do_command(cmd, 0);
}

int process_file(const char *filename)
{
	FILE *in;
	char buf[1024];
	int line_no = 0;

	in = fopen(filename, "r");
	if (!in) {
		printc_err("read: can't open %s: %s\n",
			filename, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), in)) {
		char *cmd = buf;

		line_no++;

		while (*cmd && isspace(*cmd))
			cmd++;

		if (*cmd == '#')
			continue;

		if (do_command(cmd, 0) < 0) {
			printc_err("read: error processing %s (line %d)\n",
				filename, line_no);
			fclose(in);
			return -1;
		}
	}

	fclose(in);
	return 0;
}
