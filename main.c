/*
 * Swordfish : A pkill-like CLI tool
 * License: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <fcntl.h>

#define MAX_MATCHES 1024

typedef struct
{
    pid_t pid;
    char name[256];
    char owner[64];
} proc_entry_t;

typedef struct
{
    const char *sig_str;
    int sig;
    bool do_kill;
    bool dry_run;
    bool select_mode;
    bool exact_match;
    bool print_pids_only;
    bool auto_confirm;
    const char *user;
    int pattern_start_idx;
} swordfish_args_t;

void usage(const char *prog)
{
    fprintf(stderr,
            "Swordfish : A pkill-like CLI tool\n"
            "Usage: %s -[SNkxypsu:] pattern [pattern ...]\n"
            "  -S            : Select which PIDs to kill\n"
            "  -N            : Dry-run, do not send signals\n"
            "  -k            : Actually send the signal\n"
            "  -x            : Exact match process names\n"
            "  -y            : Auto-confirm kills (skip prompt)\n"
            "  -p            : Print raw PIDs only\n"
            "  -s <SIGNAL>   : Signal to send (default TERM)\n"
            "  -u <USER>     : Filter by username\n"
            "  pattern       : One or more process name patterns\n",
            prog);
}

void help(const char *prog)
{
    printf(
        "Swordfish : A pkill-like CLI tool\n\n"
        "Usage:\n"
        "  %s [OPTIONS] pattern [pattern ...]\n\n"
        "Options:\n"
        "  -S              Select which PIDs to kill (interactive prompt)\n"
        "  -N              Dry-run mode; do not send any signals\n"
        "  -k              Actually send the signal (default is to only list matches)\n"
        "  -x              Exact match process names (default: substring match)\n"
        "  -y              Auto-confirm kills; skip prompts and sudo confirmation\n"
        "  -p              Print raw PIDs only\n"
        "  -s <SIGNAL>     Signal to send (name or number, default TERM)\n"
        "  -u <USER>       Filter processes by username\n"
        "  --help          Show this help message and exit\n\n"
        "Patterns:\n"
        "  One or more patterns to match process names against.\n"
        "  Matching is case-insensitive substring unless -x is used.\n\n"
        "Examples:\n"
        "  %s -k firefox                 Kill all processes with 'firefox' in the name\n"
        "  %s -kx bash                   Kill all exact matches of 'bash'\n"
        "  %s -Sk KILL vim               Interactively select vim processes and send SIGKILL\n"
        "  %s -ky firefox vim bash       Kill all 'firefox', 'vim', and 'bash' processes without confirmation\n",
        prog, prog, prog, prog, prog);
}

bool is_numeric(const char *s)
{
    while (*s)
    {
        if (!isdigit(*s++))
            return false;
    }
    return true;
}

int get_signal(const char *sigstr)
{
    if (is_numeric(sigstr))
    {
        int signum = atoi(sigstr);
        if (signum > 0 && signum < NSIG)
            return signum;
        return -1;
    }
    struct
    {
        const char *name;
        int sig;
    } signals[] = {
        {"HUP", SIGHUP},
        {"INT", SIGINT},
        {"QUIT", SIGQUIT},
        {"KILL", SIGKILL},
        {"TERM", SIGTERM},
        {"USR1", SIGUSR1},
        {"USR2", SIGUSR2},
        {"STOP", SIGSTOP},
        {"CONT", SIGCONT},
    };
    for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); i++)
    {
        if (strcasecmp(sigstr, signals[i].name) == 0)
            return signals[i].sig;
    }
    return -1;
}

bool substring_match(const char *haystack, const char *needle)
{
    return strcasestr(haystack, needle) != NULL;
}

bool is_proc_dir(const char *name)
{
    for (const char *p = name; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

void drop_privileges()
{
    if (geteuid() == 0)
    {
        uid_t uid = getuid();
        gid_t gid = getgid();
        if (setgid(gid) != 0 || setuid(uid) != 0)
        {
            fprintf(stderr, "Failed to drop privileges: %s\n", strerror(errno));
            exit(2);
        }
    }
}

const char *get_proc_user(uid_t uid)
{
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : "unknown";
}

int parse_args(int argc, char **argv, swordfish_args_t *args)
{
    // Check for --help before getopt
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            help(argv[0]);
            exit(0);
        }
    }

    int opt;
    args->sig_str = "TERM";
    args->sig = SIGTERM;
    args->do_kill = false;
    args->dry_run = false;
    args->select_mode = false;
    args->exact_match = false;
    args->print_pids_only = false;
    args->auto_confirm = false;
    args->user = NULL;

    while ((opt = getopt(argc, argv, "SNkxyps:u:")) != -1)
    {
        switch (opt)
        {
        case 'S':
            args->select_mode = true;
            break;
        case 'N':
            args->dry_run = true;
            break;
        case 'k':
            args->do_kill = true;
            break;
        case 'x':
            args->exact_match = true;
            break;
        case 'y':
            args->auto_confirm = true;
            break;
        case 'p':
            args->print_pids_only = true;
            break;
        case 's':
            args->sig_str = optarg;
            break;
        case 'u':
            args->user = optarg;
            break;
        default:
            usage(argv[0]);
            return 2;
        }
    }

    if (optind >= argc)
    {
        usage(argv[0]);
        return 2;
    }

    args->pattern_start_idx = optind;
    args->sig = get_signal(args->sig_str);
    if (args->sig == -1)
    {
        fprintf(stderr, "Unknown signal: %s\n", args->sig_str);
        return 2;
    }
    return 0;
}

bool pattern_matches(const swordfish_args_t *args, const char *name, char **patterns, int pattern_count)
{
    for (int i = 0; i < pattern_count; ++i)
    {
        if ((args->exact_match && strcasecmp(name, patterns[i]) == 0) ||
            (!args->exact_match && substring_match(name, patterns[i])))
        {
            return true;
        }
    }
    return false;
}

int scan_processes(const swordfish_args_t *args, char **patterns, int pattern_count)
{
    DIR *proc = opendir("/proc");
    if (!proc)
    {
        perror("opendir /proc");
        return 2;
    }

    proc_entry_t matches[MAX_MATCHES];
    int matched = 0;
    struct dirent *entry;

    while ((entry = readdir(proc)) != NULL)
    {
        if (!is_proc_dir(entry->d_name))
            continue;

        char comm_path[PATH_MAX];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);

        FILE *f = fopen(comm_path, "r");
        if (!f)
            continue;

        char name[256];
        if (!fgets(name, sizeof(name), f))
        {
            fclose(f);
            continue;
        }
        fclose(f);
        name[strcspn(name, "\n")] = 0;

        if (!pattern_matches(args, name, patterns, pattern_count))
            continue;

        // Get UID
        char status_path[PATH_MAX];
        snprintf(status_path, sizeof(status_path), "/proc/%s/status", entry->d_name);
        uid_t uid = -1;
        FILE *status = fopen(status_path, "r");
        if (status)
        {
            char line[256];
            while (fgets(line, sizeof(line), status))
            {
                if (strncmp(line, "Uid:", 4) == 0)
                {
                    sscanf(line, "Uid:\t%u", &uid);
                    break;
                }
            }
            fclose(status);
        }

        if (args->user && strcasecmp(get_proc_user(uid), args->user) != 0)
            continue;

        if (matched < MAX_MATCHES)
        {
            matches[matched].pid = atoi(entry->d_name);
            snprintf(matches[matched].name, sizeof(matches[matched].name), "%s", name);
            snprintf(matches[matched].owner, sizeof(matches[matched].owner), "%s", get_proc_user(uid));
            matched++;
        }
    }

    closedir(proc);

    if (args->print_pids_only)
    {
        for (int i = 0; i < matched; ++i)
            printf("%d\n", matches[i].pid);
        return matched > 0 ? 0 : 1;
    }

    if (matched == 0)
    {
        fprintf(stderr, "No processes matched.\n");
        return 1;
    }

    int selected[MAX_MATCHES], count = 0;
    if (args->select_mode && !args->auto_confirm)
    {
        printf("Select which processes to act on:\n");
        for (int i = 0; i < matched; ++i)
            printf("[%d] PID %d (%s)\n", i + 1, matches[i].pid, matches[i].name);

        printf("Enter numbers (e.g., 1,2,5-7) or leave empty for all: ");
        char input[256] = {0};
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;

        if (strlen(input) == 0)
        {
            for (int i = 0; i < matched; ++i)
                selected[count++] = i;
        }
        else
        {
            char *token = strtok(input, ",");
            while (token && count < matched)
            {
                char *dash = strchr(token, '-');
                if (dash)
                {
                    *dash = '\0';
                    int start = atoi(token);
                    int end = atoi(dash + 1);
                    if (start > 0 && end >= start)
                    {
                        for (int j = start; j <= end && count < matched; ++j)
                        {
                            int idx = j - 1;
                            if (idx >= 0 && idx < matched)
                                selected[count++] = idx;
                        }
                    }
                }
                else
                {
                    int idx = atoi(token) - 1;
                    if (idx >= 0 && idx < matched)
                        selected[count++] = idx;
                }
                token = strtok(NULL, ",");
            }
        }
    }
    else
    {
        for (int i = 0; i < matched; ++i)
            selected[count++] = i;
    }

    for (int i = 0; i < count; ++i)
    {
        int idx = selected[i];
        if (args->do_kill && !args->dry_run)
        {
            if (kill(matches[idx].pid, args->sig) == 0)
                printf("Sent signal %d (%s) to PID %d (%s)\n",
                       args->sig, strsignal(args->sig), matches[idx].pid, matches[idx].name);
            else
                fprintf(stderr, "Failed to kill PID %d (%s): %s\n",
                        matches[idx].pid, matches[idx].name, strerror(errno));
        }
        else if (args->dry_run)
        {
            printf("Would send signal %d (%s) to PID %d (%s)\n",
                   args->sig, strsignal(args->sig), matches[idx].pid, matches[idx].name);
        }
        else
        {
            printf("%d (%s) owned by %s\n", matches[idx].pid, matches[idx].name, matches[idx].owner);
        }
    }

    return 0;
}

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
        drop_privileges();

    return scan_processes(&args, &argv[args.pattern_start_idx], argc - args.pattern_start_idx);
}
