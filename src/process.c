#define _GNU_SOURCE
#include "process.h"
#include "args.h"
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

#define MAX_MATCHES 1024

typedef struct
{
  pid_t pid;
  char name[256];
  char owner[64];
} proc_entry_t;

static bool substring_match(const char *haystack, const char *needle)
{
  return strcasestr(haystack, needle) != NULL;
}

static bool is_proc_dir(const char *name)
{
  for (const char *p = name; *p; p++)
    if (!isdigit(*p))
      return false;
  return true;
}

void drop_privileges(void)
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

static const char *get_proc_user(uid_t uid)
{
  struct passwd *pw = getpwuid(uid);
  return pw ? pw->pw_name : "unknown";
}

static bool pattern_matches(const swordfish_args_t *args, const char *name, char **patterns, int pattern_count)
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

  // Confirmation prompt for killing processes (unless auto_confirm)
  if (args->do_kill && !args->dry_run && !args->auto_confirm && count > 0)
  {
    printf("The following processes will be killed (signal %d - %s):\n", args->sig, strsignal(args->sig));
    for (int i = 0; i < count; ++i)
    {
      int idx = selected[i];
      printf("  PID %d (%s) owned by %s\n", matches[idx].pid, matches[idx].name, matches[idx].owner);
    }
    printf("Proceed? [y/N]: ");
    char confirm[8] = {0};
    fgets(confirm, sizeof(confirm), stdin);
    if (confirm[0] != 'y' && confirm[0] != 'Y')
    {
      printf("Aborted.\n");
      return 0;
    }
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
