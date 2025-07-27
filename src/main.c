/*
 * Swordfish : A pkill-like CLI tool
 * License: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "args.h"
#include "process.h"

int main(int argc, char **argv)
{
    swordfish_args_t args;
    if (parse_args(argc, argv, &args) != 0)
        return 2;

    // Auto-sudo if trying to send signals and not root
    if (args.do_kill && geteuid() != 0)
    {
        if (args.auto_confirm)
        {
            // Re-run with sudo without asking
            char **new_argv = malloc((argc + 2) * sizeof(char *));
            if (!new_argv)
            {
                perror("malloc");
                return 2;
            }
            new_argv[0] = "sudo";
            for (int i = 0; i < argc; ++i)
                new_argv[i + 1] = argv[i];
            new_argv[argc + 1] = NULL;
            execvp("sudo", new_argv);
            perror("execvp failed");
            free(new_argv);
            return 2;
        }
        else
        {
            fprintf(stderr, "Warning: Sending signals may require root privileges.\n");
            printf("Rerun with sudo? [y/N]: ");
            char response[8] = {0};
            fgets(response, sizeof(response), stdin);
            if (response[0] == 'y' || response[0] == 'Y')
            {
                char **new_argv = malloc((argc + 2) * sizeof(char *));
                if (!new_argv)
                {
                    perror("malloc");
                    return 2;
                }
                new_argv[0] = "sudo";
                for (int i = 0; i < argc; ++i)
                    new_argv[i + 1] = argv[i];
                new_argv[argc + 1] = NULL;
                execvp("sudo", new_argv);
                perror("execvp failed");
                free(new_argv);
                return 2;
            }
        }
    }

    // Drop privs if no -k but running as root
    if (!args.do_kill && geteuid() == 0)
    {
        drop_privileges();
    }

    return scan_processes(&args, &argv[args.pattern_start_idx], argc - args.pattern_start_idx);
}
