#define _GNU_SOURCE
#include "tmpfs_switch_init.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void maybe_activate(bool argc1, const char *arg0) {
  const char *basename = strrchr(arg0, '/');
  if (strcmp("activate", basename ? basename + 1 : arg0) != 0) {
    puts("Must run as pid 1");
    return;
  }
  if (!argc1) {
    puts("Too many arguments");
    return;
  }
  struct stat root, target;
  if (lstat("/", &root))
    abort();
  if (lstat("/cdrom", &target)) {
    puts("New root missing");
    return;
  }
  if (!S_ISDIR(target.st_mode)) {
    puts("New root is not a directory");
    return;
  }
  if (target.st_nlink < 5) {
    puts("New root lacks files");
    return;
  }
  if (root.st_dev == target.st_dev) {
    puts("New root is not a mountpoint");
    return;
  }
  execl("/bin/systemctl", "systemctl", "switch-root", "/cdrom",
        "/sbin/tmpfs_switch_init", NULL);
  abort();
}

const char *init_environ[] = {
    "HOME=/root",
    "PAGER=less",
    "TERM=linux",
    "SHELL=/bin/bash",
    "USER=root",
    // usrmerge, and don't care about local
    "PATH=/sbin:/bin",
    "PWD=/",
};

static void setup_env() {
  if (mkdir("/root", 0700) && errno != EEXIST) {
    puts("mkdir root failed");
    environ[0] = "HOME=/";
  }
  if (clearenv() || chdir("/"))
    abort();
}

static void try_mount(const char *mountpoint, const char *type) {
  assert(mountpoint[0] == '/' && mountpoint[1] && type[0]);
  (void) umount2(mountpoint, MNT_FORCE | MNT_DETACH);
  unsigned long flags = MS_NOSUID | MS_NOEXEC | MS_NOATIME;
  if (mountpoint[1] != 'd')
    flags |= MS_NODEV;
  if (mount("none", mountpoint, type, flags, NULL))
    abort();
}

static void mount_normal(void) {
  // no need for /run as there are no users and rootfs is already tmpfs
  (void)umount2("/run", MNT_FORCE | MNT_DETACH);
  static const char *const MOUNTPOINTS[][2] = {
      {"/dev/pts", "devpts"},
      {"/dev/shm", "tmpfs"},
      {"/dev/mqueue", "mqueue"},
      {"/sys/kernel/security", "securityfs"},
      {"/sys/fs/cgroup", "cgroup2"},
      {"/sys/fs/pstore", "pstore"},
      {"/sys/fs/bpf", "bpf"},
      // no need for hugetlbfs in recovery
      {"/sys/kernel/debug", "debugfs"},
      {"/sys/kernel/tracing", "tracefs"},
      // no fuse in recovery
      {"/sys/kernel/config", "configfs"},
      {"/sys", "sysfs"},
  };
  for (size_t i = sizeof(MOUNTPOINTS) / sizeof(MOUNTPOINTS[1]); i--;) {
    try_mount(MOUNTPOINTS[i][0], MOUNTPOINTS[i][1]);
  }
}

int main(int argc, char *argv[]) {
  if (!argc) {
    puts("Expected process name");
    return 1;
  }
  if (getuid()) {
    puts("tmpfs_switch_init must be root");
    return 1;
  }
  if (getpid() != 1) {
    maybe_activate(argc == 1, *argv);
    return 1;
  }
  (void)unlink("/activate");
  sync();
  const enum sleep1_error sleep1_error = sleep1();
  try_mount("/dev", "devtmpfs");
  activate_tty_early();
  printf("tmpfs_switch_init with argc %d\n", argc);
  for (int i = 0; i < argc; ++i) {
    puts(argv[i]);
  }
  print_sleep1_error(sleep1_error);
  {
    static const struct sigaction sa = {
        .sa_handler = SIG_DFL,
        .sa_flags = SA_NOCLDSTOP,
    };
    if (sigaction(SIGCHLD, &sa, NULL))
      abort();
  }
  if (reboot(RB_DISABLE_CAD) || signal(SIGHUP, SIG_IGN))
    abort();
  try_mount("/proc", "proc");
  killall5();
  setup_env();
  mount_normal();
  sync();
  if (signal(SIGCHLD, SIG_DFL))
    abort();
  print_sleep1_error(sleep1());
  release_tty();
  activate_tty_late();
  puts("work shell");
  const char *const bash_argv[] = {
      "bash",
      NULL,
  };
  const pid_t work_pid = fork_exec_tty(false, "/bin/bash", bash_argv);
  for (;;) {
    const pid_t lost_pid = wait(NULL);
    if (lost_pid <= 1) {
      if (errno != EINTR)
        abort();
      continue;
    }
    if (lost_pid == work_pid)
      break;
  }
  activate_tty_late();
  puts("work done");
  print_sleep1_error(sleep1());
  killall5();
  disallocate_ttys();
  puts("exec shell");
  fork_exec_tty(true, "/bin/bash", bash_argv);
  abort();
}
