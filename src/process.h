#ifndef PROCESS_H
#define PROCESS_H
#include "args.h"

int scan_processes(const swordfish_args_t *args, char **patterns, int pattern_count);
void drop_privileges(void);

#endif // PROCESS_H
