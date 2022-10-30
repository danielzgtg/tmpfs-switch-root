#define _GNU_SOURCE
#include "tmpfs_switch_init.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static pid_t str_to_pid(const char *c) {
  pid_t pid = 0;
  do {
    if (*c < '0' || *c > '9')
      return ~0;
    pid = pid * 10 + *c - '0';
  } while (*++c);
  return pid;
}

static void handle_pid(int proc, pid_t pid, const char *d_name, bool term) {
    {
      const int maps = openat(proc, "maps", O_RDONLY | O_CLOEXEC);
      if (maps < 0) {
#ifndef NDEBUG
        printf("disappeared maps %s\n", d_name);
#endif
        return;
      }
      char ignore;
      ssize_t len = read(maps, &ignore, 1);
      if (len < 0) {
#ifndef NDEBUG
        printf("unreadable maps %s\n", d_name);
#endif
        len = 0;
      }
      if (close(maps)) {
#ifndef NDEBUG
        printf("error closing maps %s\n", d_name);
#endif
      }
      if (!len) {
        // kernel thread
        return;
      }
    }
    {
      const int cmdline = openat(proc, "cmdline", O_RDONLY | O_CLOEXEC);
      if (cmdline < 0) {
#ifndef NDEBUG
        printf("disappeared cmdline %s\n", d_name);
#endif
        return;
      }
      char name[256];
      ssize_t len = read(cmdline, &name, sizeof(name));
      assert(len <= (ssize_t)sizeof(name));
      if (len < 0) {
#ifndef NDEBUG
        printf("unreadable cmdline %s\n", d_name);
#endif
      } else if (!len) {
        printf("missing cmdline %s\n", d_name);
        len = 1;
      } else if (len == 1) {
        if (!name[0]) {
          printf("empty cmdline %s\n", d_name);
        } else {
          printf("corrupt empty cmdline %s\n", d_name);
        }
      } else if (len == sizeof(name) && !memchr(name, '\0', sizeof(name))) {
        printf("oversized cmdline %s\n", d_name);
      } else if (len != sizeof(name) && name[len - 1]) {
        printf("corrupt cmdline %s\n", d_name);
      } else {
        len = 0;
      }
      if (close(cmdline)) {
#ifndef NDEBUG
        printf("error closing cmdline %s\n", d_name);
#endif
      }
      if (len)
        return;
      if (term) {
        printf("remaining %s %s\n", d_name, name);
        kill(pid, SIGTERM);
      } else {
        printf("stubborn %s %s\n", d_name, name);
        kill(pid, SIGKILL);
      }
    }
}

static void kill_processes(int procfs, bool term) {
  static char buf[16384]; // this init is not threaded
  for (ssize_t end; (end = getdents64(procfs, buf, sizeof(buf)));) {
    if (!~end) {
      puts("getdents error");
      abort();
    }
    const struct dirent64 *entry;
    for (ssize_t i = 0; i < end; i += entry->d_reclen) {
      entry = (const struct dirent64 *) (buf + i);
      assert(entry->d_type != DT_UNKNOWN);
      if (entry->d_type != DT_DIR)
        continue;
      pid_t pid = str_to_pid(entry->d_name);
      if (pid <= 1)
        continue;
      const int proc = openat(procfs, entry->d_name, O_CLOEXEC | O_DIRECTORY | O_PATH);
      if (proc < 0) {
#ifndef NDEBUG
        printf("disappeared pid %s\n", entry->d_name);
#endif
        continue;
      }
      handle_pid(proc, pid, entry->d_name, term);
      if (close(proc)) {
#ifndef NDEBUG
        printf("error closing pid %s\n", entry->d_name);
#endif
      }
    }
  }
}

void killall5(void) {
  const int procfs = open("/proc", O_CLOEXEC | O_DIRECTORY);
  if (procfs < 0)
    abort();
  puts("sigterm");
  kill_processes(procfs, true);
  print_sleep1_error(sleep1());
  puts("sigkill");
  if (lseek(procfs, 0, SEEK_SET))
    abort();
  kill_processes(procfs, false);
  if (close(procfs)) {
#ifndef NDEBUG
    puts("error closing procfs");
#endif
  }
  puts("done killall5");
}

enum sleep1_error sleep1(void) {
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time)) {
    return SLEEP1_ERROR_GET;
  } else {
    time.tv_sec += 1;
    if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, NULL)) {
      return SLEEP1_ERROR_SLEEP;
    }
  }
  return SLEEP1_ERROR_NONE;
}

void print_sleep1_error(enum sleep1_error sleep1_error) {
  switch (sleep1_error) {
  case SLEEP1_ERROR_GET:
    puts("clock_gettime failed");
    break;
  case SLEEP1_ERROR_SLEEP:
    puts("clock_nanosleep failed");
    break;
  default:
    break;
  }
}
