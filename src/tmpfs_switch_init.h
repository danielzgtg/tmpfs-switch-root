#pragma once

#include "work_file.h"
#include <stdbool.h>
#include <sys/types.h>

void release_tty(void);

void killall5();

enum sleep1_error {
  SLEEP1_ERROR_NONE = 0,
  SLEEP1_ERROR_GET = 1,
  SLEEP1_ERROR_SLEEP = 2,
};

enum sleep1_error sleep1(void);

void print_sleep1_error(enum sleep1_error sleep1_error);

void activate_tty_early(void);

void activate_tty_late(void);

extern const char *init_environ[];

pid_t fork_exec_tty(bool no_fork, const char *path, const char *const *argv);

void disallocate_ttys(void);
