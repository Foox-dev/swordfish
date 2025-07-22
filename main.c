/*
 * Swordfish : A pkill-like cli tool
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

void usage(const char *prog)
{
    fprintf(stderr,
            "Swordfish : A pkill-like cli tool\n"
            "Usage: %s [-s signal] [-k] [-n|--dry-run] pattern\n"
            "  -s signal : Signal to send (name or number), default TERM\n"
            "  -k        : Kill matched processes (requires root)\n"
            "  -n, --dry-run : Show what would be killed, but do not send signals\n"
            "  pattern   : Substring to match in process name\n",
            prog);
}

int is_numeric(const char *s)
{
    while (*s)
    {
        if (!isdigit(*s++))
            return 0;
    }
    return 1;
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
    return -1; // unknown signal
}

bool substring_match(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++)
    {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return true;
    }
    return false;
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
            exit(1);
        }
    }
}

int main(int argc, char **argv)
{
    int opt;
    const char *sig_str = "TERM";
    int sig = SIGTERM;
    bool do_kill = false;
    bool dry_run = false;

    // getopt_long for --dry-run
    while ((opt = getopt(argc, argv, "s:kn")) != -1)
    {
        switch (opt)
        {
        case 's':
            sig_str = optarg;
            break;
        case 'k':
            do_kill = true;
            break;
        case 'n':
            dry_run = true;
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    // Check for --dry-run manually
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--dry-run") == 0)
        {
            dry_run = true;
        }
    }
    if (optind >= argc)
    {
        usage(argv[0]);
        return 1;
    }
    sig = get_signal(sig_str);
    if (sig == -1)
    {
        fprintf(stderr, "Unknown signal: %s\n", sig_str);
        return 1;
    }
    if (do_kill && geteuid() != 0)
    {
        fprintf(stderr, "Warning: killing processes requires root privileges\n");
    }
    if (!do_kill && geteuid() == 0)
    {
        drop_privileges();
    }
    const char *pattern = argv[optind];
    DIR *proc = opendir("/proc");
    if (!proc)
    {
        perror("opendir /proc");
        return 2;
    }
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
        {
            fprintf(stderr, "Could not open %s: %s\n", comm_path, strerror(errno));
            continue;
        }
        char *line = NULL;
        size_t len = 0;
        ssize_t read_len = getline(&line, &len, f);
        fclose(f);
        if (read_len <= 0)
        {
            if (line)
                free(line);
            continue;
        }
        if (line[read_len - 1] == '\n')
            line[read_len - 1] = '\0';
        if (substring_match(line, pattern))
        {
            matched++;
            pid_t pid = (pid_t)atoi(entry->d_name);
            if (do_kill && !dry_run)
            {
                if (kill(pid, sig) == 0)
                {
                    printf("Sent signal %d (%s) to PID %d (%s)\n", sig, strsignal(sig), pid, line);
                }
                else
                {
                    fprintf(stderr, "Failed to kill PID %d (%s): %s\n", pid, line, strerror(errno));
                }
            }
            else if (do_kill && dry_run)
            {
                printf("Would send signal %d (%s) to PID %d (%s)\n", sig, strsignal(sig), pid, line);
            }
            else
            {
                printf("Matched PID %d (%s)\n", pid, line);
            }
        }
        if (line)
            free(line);
    }
    closedir(proc);
    if (matched == 0)
    {
        fprintf(stderr, "No processes matched pattern '%s'\n", pattern);
        return 1;
    }
    return 0;
}
