#define _GNU_SOURCE

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "help.h"
#include "main.h"
#include "process.h"
#include "tui.h"

int main(int arg_count, char **argv) {
	swordfish_args_t args;
	int argc = arg_count;
	int ret = parse_args(&argc, argv, &args);

	if (strcmp(argv[1], "-h") == 0) {
		usage(argv[0]);
		return 0;
	}

	if (args.help_topic != NULL || (argc > 1 && (strcmp(argv[1], "--help") == 0))) {
		help(args.help_topic);
		return 0;
	}
	if (ret)
		return ret;

	int pattern_count = argc - args.pattern_start_idx;
	char **patterns = &argv[args.pattern_start_idx];

	/* If args contains an empty string, ask user for confirmation.
	   We dont check if pattern_count is 0 because parse_args already handles that. */
	bool has_empty_pattern = false;
	for (int i = 0; i < pattern_count; ++i) {
		if (patterns[i][0] == '\0') {
			has_empty_pattern = true;
			break;
		}
	}

	if (has_empty_pattern && !args.auto_confirm && is_interactive()) {
		WARN("Empty pattern specified. \nThis may match all processes and could be " COLOR_WARN
		     "dangerous!" COLOR_RESET);
		printf("Proceed? [y/N]: ");
		char confirm[8] = {0};
		(void)fgets(confirm, sizeof(confirm), stdin);
		if (tolower((unsigned char)confirm[0]) != 'y') {
			return 1;
		}
	}

	bool pattern_is_pid[pattern_count + 1];
	for (int i = 0; i < pattern_count; ++i)
		pattern_is_pid[i] = !!is_all_digits(patterns[i]);

	char pidfile_pid_str[16] = {0};
	if (args.pidfile) {
		FILE *f = fopen(args.pidfile, "r");
		if (!f) {
			ERROR("Cannot open pidfile: %s", args.pidfile);
			return 1;
		}
		pid_t pid = 0;
		if (fscanf(f, "%d", &pid) != 1 || pid <= 0) {
			fclose(f);
			ERROR("Invalid PID in pidfile: %s", args.pidfile);
			return 1;
		}
		fclose(f);
		snprintf(pidfile_pid_str, sizeof(pidfile_pid_str), "%d", pid);
		patterns[pattern_count] = pidfile_pid_str;
		pattern_is_pid[pattern_count] = true;
		pattern_count++;
	}

	pattern_list_t plist = {
		.patterns = patterns,
		.pattern_is_pid = pattern_is_pid,
		.pattern_count = pattern_count,
	};

	return scan_processes(&args, &plist);
}
