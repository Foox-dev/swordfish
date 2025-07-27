#include "args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>

#ifndef NSIG
#define NSIG 65
#endif

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

static bool is_numeric(const char *s)
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
