#ifndef ARGS_H
#define ARGS_H
#include <stdbool.h>
#include <signal.h>

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
  bool do_verbose;
  const char *user;
  int pattern_start_idx;
} swordfish_args_t;

void usage(const char *prog);
void help(const char *prog);
int parse_args(int argc, char **argv, swordfish_args_t *args);
int get_signal(const char *sigstr);

#endif // ARGS_H
