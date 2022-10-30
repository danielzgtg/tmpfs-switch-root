#include "work_file.h"
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/mount.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#define INIT_BIN_NAME "tmpfs_switch_init"

using namespace std;

namespace {
void AssertEmptyDir(const char *path) {
  if (path[0] != '/' || strchr(path + 1, '/'))
    abort();
  struct stat root {
  }, target{};
  if (lstat(path, &target))
    abort();
  if (!S_ISDIR(target.st_mode) || target.st_nlink != 2)
    abort();
  if (lstat("/", &root))
    abort();
  if (root.st_dev != target.st_dev)
    abort();
}

int Mount() {
  AssertEmptyDir(TARGET_DIR);
  if (mount("none", TARGET_DIR, "tmpfs", MS_NODEV | MS_NOSUID | MS_NOATIME,
            "size=1048576k,mode=700"))
    abort();
  const int dir = open(TARGET_DIR, O_CLOEXEC | O_DIRECTORY | O_PATH);
  if (dir < 0)
    abort();
  return dir;
}

void UsrMerge(int dir) {
  static const char *const FOLDERS[]{
      "boot", "dev", "mnt", "proc", "root",  "run",
      "sys",  "tmp", "var", "usr",  nullptr,
  };
  static const char *const LINKS[]{
      "usr/bin",  "usr/lib",   "usr/lib32", "usr/lib64",
      "usr/sbin", "usr/share", nullptr,
  };
  for (auto i = FOLDERS; *i; ++i) {
    if (mkdirat(dir, *i, 0700))
      abort();
  }
  static constexpr size_t USR_PREFIX_LENGTH = "usr/"sv.size();
  for (auto i = LINKS; *i; ++i) {
    if (mkdirat(dir, *i, 0700) || symlinkat(*i, dir, *i + USR_PREFIX_LENGTH))
      abort();
  }
}

void SendFileImpl(int output, int input, bool exe) {
  ssize_t rem;
  {
    struct stat st {};
    if (fstat(input, &st))
      abort();
    rem = st.st_size;
    if (rem < 0 || rem > 134217728) // 128MiB
      abort();
    if (!rem && exe)
      abort();
  }
  if (rem) {
    ssize_t ret{};
    do {
      rem -= ret;
      ret = sendfile(output, input, nullptr, rem);
      if (ret < 0) {
        cout << errno << endl;
        abort();
      }
    } while (ret && ret < rem);
    if (ret != rem)
      abort();
  }
  if (close(output) || close(input))
    abort();
}

void SendFile(int dir, const char *desc) {
  const char type = *desc;
  const char *path = desc + 1;
  switch (type) {
  case COPY_EXE:
  case COPY_DAT: {
    const int input = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0)
      abort();
    const int output =
        openat(dir, path + 1, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL,
               type == COPY_EXE ? 0700 : 0600);
    if (output < 0)
      abort();
    SendFileImpl(output, input, type == COPY_EXE);
    break;
  }
  case COPY_DIR:
    if (mkdirat(dir, path + 1, 0700) && errno != EEXIST)
      abort();
    break;
  case COPY_LNK: {
    static constexpr size_t MAX_LINK_SIZE = 255;
    static_assert(MAX_LINK_SIZE < PATH_MAX);
    char buffer[MAX_LINK_SIZE];
    const ssize_t lnk_size = readlink(path, buffer, MAX_LINK_SIZE);
    if (lnk_size <= 0 || lnk_size == MAX_LINK_SIZE)
      abort();
    char existing[MAX_LINK_SIZE];
    const ssize_t original_size =
        readlinkat(dir, path + 1, existing, MAX_LINK_SIZE);
    if (original_size >= 0) {
      if (original_size != lnk_size || !!memcmp(buffer, existing, lnk_size))
        abort();
    } else {
      buffer[lnk_size] = '\0';
      if (symlinkat(buffer, dir, path + 1))
        abort();
    }
    break;
  }
  default:
    abort();
  }
}

[[maybe_unused]] void SendFiles(int dir, ifstream f) {
  string l{};
  l.reserve(100);
  while (getline(f, l)) {
    if (l.empty())
      continue;
    if (l[1] != '/' || l[2] == '/')
      abort();
    SendFile(dir, l.c_str());
  }
}

void SendInit(int fd, int dir) {
  static_cast<void>(unlinkat(dir, "sbin/init", 0));
  if (symlinkat("tmpfs_switch_init", dir, "sbin/init") ||
      symlinkat("sbin/tmpfs_switch_init", dir, "activate"))
    abort();
  const int output = openat(dir, "sbin/tmpfs_switch_init",
                            O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0700);
  // sendfile does not work in a virtualbox shared folder as of 6.1.38. Oh well.
  SendFileImpl(output, fd, true);
}

void Run() {
  ifstream f{WORK_FILE_NAME};
  if (!f)
    abort();
  const int init =
      open(INIT_BIN_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (init < 0)
    abort();
  const int dir = Mount();
  UsrMerge(dir);
  SendFiles(dir, std::move(f));
  SendInit(init, dir);
  if (close(dir))
    abort();
}
} // namespace

int main() {
  if (getuid()) {
    puts("Root is required to build the ramdisk");
    return 1;
  }
  if (!filesystem::is_regular_file(WORK_FILE_NAME)) {
    puts("Please run ./gather_file_info first");
    return 1;
  }
  if (!filesystem::is_regular_file(INIT_BIN_NAME)) {
    puts("./" INIT_BIN_NAME " is missing");
    return 1;
  }
  Run();
  return 0;
}
