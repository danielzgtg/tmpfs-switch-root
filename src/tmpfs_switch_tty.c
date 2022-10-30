#define _GNU_SOURCE
#include "tmpfs_switch_init.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/tiocl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Documentation sources:
// - man stty
// - man tput
// - man reset
// - man ioctl_tty
// - man ioctl_console
// - man termios
// - man console_codes
// - busybox init.c
// - systemd terminal-util.c
// No code was copy-and-pasted and this is under a compatible license

void release_tty(void) {
  puts("reltty");
  const int dev_tty = open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY);
  if (dev_tty < 0) {
    if (errno != ENXIO)
      abort();
  } else if (ioctl(dev_tty, TIOCNOTTY) || close(dev_tty))
    abort();
  puts("relttydone");
}

static void use_tty_closing_leaks(int fd) {
  if ((fd != 3 && dup2(fd, 3) != 3) || close_range(4, ~0, 0) || dup2(3, 0) ||
      dup2(3, 1) != 1 || dup2(3, 2) != 2 || close(3))
    abort();
}

static void clear_screen() {
  fputs("\x1b[r\x1b[H\x1b[2J\x1b[3J\x1b[0m\x1b"
        "c",
        stdout);
  fflush(stdout);
}

#define IOCTL_CHECK(x) (x)

static void setup_tty(void) {
  struct tioclinux {
    char a;
    char b;
  };
  const int debug = open("/ttyS0", O_RDWR | O_CLOEXEC | O_NOCTTY);
  dup2(debug, 2);
  static const struct tioclinux unblank = {.a = TIOCL_UNBLANKSCREEN};
  IOCTL_CHECK(ioctl(0, TIOCLINUX, &unblank));
  static const struct tioclinux redirect = {
      .a = TIOCL_SETKMSGREDIRECT,
      .b = 0,
  };
  clear_screen();
  IOCTL_CHECK(ioctl(0, TIOCLINUX, &redirect));
  IOCTL_CHECK(ioctl(0, KDSETLED, ~0));
  IOCTL_CHECK(ioctl(0, KDSKBLED, 0));
  IOCTL_CHECK(ioctl(0, KDSETMODE, KD_TEXT));
  IOCTL_CHECK(ioctl(0, KDMKTONE, 0));
  IOCTL_CHECK(ioctl(0, KIOCSOUND, 0));
  IOCTL_CHECK(ioctl(0, PIO_FONTRESET, NULL));
  IOCTL_CHECK(ioctl(0, KDSKBMODE, K_UNICODE));
  static const struct vt_mode vt_mode = {
      .mode = VT_AUTO,
  };
  IOCTL_CHECK(ioctl(0, VT_SETMODE, &vt_mode));
  IOCTL_CHECK(ioctl(0, VT_UNLOCKSWITCH));
  IOCTL_CHECK(ioctl(0, TIOCNXCL));
  static const int discipline = N_TTY;
  IOCTL_CHECK(ioctl(0, TIOCSETD, &discipline));
  IOCTL_CHECK(ioctl(0, TIOCCBRK));
  static const struct tioclinux vesa = {
      .a = TIOCL_SETVESABLANK,
      .b = 0,
  };
  IOCTL_CHECK(ioctl(0, TIOCLINUX, &vesa));
  struct termios termios;
  memset(&termios, 0, sizeof(termios));
  IOCTL_CHECK(ioctl(0, TIOCSLCKTRMIOS, &termios));
  if (ioctl(0, TCGETS, &termios)) {
    abort();
  }
#define ENABLE_UTF 1
  termios.c_iflag = ICRNL | IXON | IXOFF;
  termios.c_iflag |= ENABLE_UTF ? IUTF8 : ISTRIP;
  // Ignore stuff in this recovery mode
  termios.c_iflag |= IGNBRK | IGNPAR | IUTF8;
  // Undesired -BRKINT -PARMCK -INPCK -INLCR -IGNCR -IUCLC -IXANY
  // NYI -IMAXBEL
  termios.c_oflag = OPOST | ONLCR | XTABS;
  // +NL0 +CR0 +BS0 +VT0 +FF0
  // Undesired -OLCUC -OCRNL -ONOCR -ONLRET -OFILL
  // NYI -OFDEL
  termios.c_cflag = B38400 | (ENABLE_UTF ? CS8 : CS7); // Assume linux tty
#undef ENABLE_UTF
  // -CBAUDEX -CSTOPB -PARENB -PARODD -HUPCL -CMSPAR -CRTSCTS
  termios.c_cflag &=
      CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD | CMSPAR | CRTSCTS;
  termios.c_cflag |= CREAD | CLOCAL;
  // Assume not serial port:
  // NYI -LOBLK -CIBAUD
  termios.c_lflag =
      ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
  // NYI -XCASE -DEFECHO -FLUSHO -PENDIN
  // Undesired -ECHONL -ECHOPRT -NOFLSH -TOSTEP
  termios.c_line = N_TTY;
  memset(&termios.c_cc, 0, sizeof(termios.c_cc));
  static const cc_t c_cc[NCCS] = {
      [VDISCARD] = 'o' - 'a' + 1,
      // NYI [VDSUSP] = 'y' - 'a' + 1,
      [VEOF] = 'd' - 'a' + 1,
      [VEOL] = 0,
      [VEOL2] = 0,
      [VERASE] = 0177,
      [VINTR] = 'c' - 'a' + 1,
      [VKILL] = 'u' - 'a' + 1,
      [VLNEXT] = 'v' - 'a' + 1,
      [VMIN] = 1,
      [VQUIT] = 034,
      [VREPRINT] = 'r' - 'a' + 1,
      [VSTART] = 'q' - 'a' + 1,
      // NYI [VSTATUS] = 't' - 'a' + 1,
      [VSTOP] = 's' - 'a' + 1,
      [VSUSP] = 'z' - 'a' + 1,
      [VSWTC] = 0,
      [VTIME] = 0,
      [VWERASE] = 'w' - 'a' + 1,
  };
  _Static_assert(sizeof(c_cc) == sizeof(termios.c_cc));
  _Static_assert(_POSIX_VDISABLE == 0);
  memcpy(&termios.c_cc, c_cc, sizeof(c_cc));
  if (ioctl(0, TCSETSF, &termios))
    abort();
  clear_screen();
  struct vt_stat vt;
  static const struct tioclinux fg = {.a = TIOCL_GETFGCONSOLE};
  if (ioctl(0, VT_GETSTATE, &vt)) {
    vt.v_active = -2;
  }
  printf("switched to console %d / %d\n", ioctl(0, TIOCLINUX, &fg),
         vt.v_active);
}

void activate_tty_early(void) {
  const int fd = open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY);
  if (fd >= 0) {
    use_tty_closing_leaks(fd);
  } else {
    activate_tty_late();
    return;
  }
  setup_tty();
}

void activate_tty_late(void) {
  const int fd1 = open("/dev/tty1", O_RDWR | O_CLOEXEC | O_NOCTTY);
  if (fd1 < 0 || ioctl(fd1, TIOCVHANGUP) || close(fd1))
    abort();
  const int fd = open("/dev/tty1", O_RDWR | O_CLOEXEC | O_NOCTTY);
  if (fd < 0)
    abort();
  use_tty_closing_leaks(fd);
  IOCTL_CHECK(ioctl(0, VT_ACTIVATE, 1));
  IOCTL_CHECK(ioctl(0, VT_WAITACTIVE, 1));
  setup_tty();
}

pid_t fork_exec_tty(bool no_fork, const char *path, const char *const *argv) {
  IOCTL_CHECK(ioctl(0, TCFLSH, TCIFLUSH));
  if (!no_fork) {
    const pid_t pid = fork();
    if (pid < 0)
      abort();
    if (pid)
      return pid;
    if (setsid() < 0)
      abort();
  }
  const int fd = open("/dev/tty1", O_RDWR | O_CLOEXEC);
  if (fd < 0)
    abort();
  use_tty_closing_leaks(fd);
  execve(path, (char *const *)argv, (char *const *)init_environ);
  abort();
}

void disallocate_ttys(void) {
  fputs("\x1b[0m", stdout);
  fflush(stdout);
  char dev[11] = "/dev/tty  ";
  for (unsigned int i = 63; i; --i) {
    if (i >= 10) {
      dev[8] = i / 10 + '0';
      dev[9] = i % 10 + '0';
    } else {
      dev[8] = i + '0';
      dev[9] = '\0';
    }
    const int fd = open(dev, O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (fd < 0 || ioctl(fd, VT_DISALLOCATE) || close(fd))
      abort();
  }
}
