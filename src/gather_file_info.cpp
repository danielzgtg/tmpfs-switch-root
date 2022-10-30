#include "trie.h"
#include "work_file.h"
#include <cassert>
#include <ext/stdio_filebuf.h>
#include <fcntl.h>
#include <filesystem>
#include <set>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>

using namespace std;

namespace {
struct Options {
  const Trie exclude_paths;
  const vector<string> include_dirs;
  const vector<string> include_pkgs;
};

template <bool PKG_ELSE_PATH>
[[nodiscard]] vector<string> LoadFileLines(const char *const path) {
  ifstream f{path};
  if (!f)
    abort();
  vector<string> result{};
  result.reserve(50);
  for (string l; getline(f, l);) {
    if (l.empty())
      continue;
    if constexpr (PKG_ELSE_PATH) {
      if (l.contains(' '))
        abort();
    } else {
      if (l[0] != '/')
        abort();
      if (l[1] == '.')
        continue;
    }
    result.push_back(std::move(l));
  }
  return result;
}

[[nodiscard]] Options LoadCustomFileList() {
#define CONFIG_PATH "config/"
  Trie exclude_paths;
  for (const string &exclude :
       LoadFileLines<false>(CONFIG_PATH "exclude_paths.txt")) {
    if (exclude[0] != '/' || exclude.ends_with('.'))
      abort();
    exclude_paths.AddPath(string_view{exclude}.substr(1));
  }
  return {
      .exclude_paths{std::move(exclude_paths)},
      .include_dirs{LoadFileLines<false>(CONFIG_PATH "include_dirs.txt")},
      .include_pkgs{LoadFileLines<true>(CONFIG_PATH "include_packages.txt")},
  };
#undef CONFIG_PATH
}

template <typename T> class Subprocess {
  pid_t child;
  __gnu_cxx::stdio_filebuf<char> p;
  istream is;

  int Init(const void *args) {
    int pipefd[2];
    if (pipe(pipefd))
      abort();
    pid_t pid = fork();
    if (pid < 0)
      abort();
    if (!pid) {
      if (close(pipefd[0]) || dup2(pipefd[1], 1) != 1 || close(pipefd[1]))
        abort();
      T::Exec(args);
    }
    if (close(pipefd[1]))
      abort();
    child = pid;
    return pipefd[0];
  }

protected:
  bool GetLineImpl(string &s) {
    do {
      if (!getline(is, s)) {
        return false;
      }
    } while (s.empty() || !T::WantsLine(s));
    return true;
  }

public:
  explicit Subprocess(const void *args)
      : child{}, p{Init(args), ios::in}, is{&p} {}

  ~Subprocess() {
    int wstatus;
    waitpid(child, &wstatus, 0);
    if (!WIFEXITED(wstatus))
      abort();
  }

  Subprocess(const Subprocess &) = delete;
  Subprocess &operator=(const Subprocess &) = delete;
  Subprocess(Subprocess &&) = delete;
  Subprocess &operator=(Subprocess &&) = delete;
};

class DpkgQuery : Subprocess<DpkgQuery> {
  friend Subprocess;

protected:
  [[noreturn]] static void Exec(const void *args) {
    const auto format = static_cast<const char *>(args);
    execl("/usr/bin/dpkg-query", "dpkg-query", "-W", format, NULL);
    abort();
  }

  static bool WantsLine(const string &s) {
    if (s[s.length() - 1] == '\t')
#if 0
      // TODO why doesn't libwacom-surface have this?
      abort();
#else
      return false;
#endif
    return true;
  }

public:
  explicit DpkgQuery(const char *format) : Subprocess{format} {}

  [[nodiscard]] string::size_type GetLine(string &s) {
    if (!GetLineImpl(s))
      return 0;
    const size_t result = s.find('\t');
    if (result + 1 <= 1 || result != s.rfind('\t')) {
      abort();
    }
    return result;
  }

#define DpkgQueryFormat(x) ("--showformat=${Package}\t${" x "}\n")
};

class AptCache : Subprocess<AptCache> {
  friend Subprocess;

protected:
  [[noreturn]] static void Exec(const void *args) {
    const auto argv = static_cast<char *const *>(args);
    execv("/usr/bin/apt-cache", argv);
    abort();
  }

  static bool WantsLine(const string &s) {
    return s.starts_with(PREFIX);
  }

public:
  static constexpr string_view PREFIX = "  Depends: ";

  explicit AptCache(char *const *argv) : Subprocess{argv} {}

  bool GetLine(string &s) {
    return GetLineImpl(s);
  }
};

set<string> LoadDpkgNecessary() {
  set<string> roots;
  string s;
  s.reserve(100);
  {
    DpkgQuery q{DpkgQueryFormat("Priority")};
    for (string::size_type tab; (tab = q.GetLine(s));) {
      const string_view attr = string_view{s}.substr(tab + 1);
      if (attr != "important" && attr != "required") {
        continue;
      }
      roots.insert(s.substr(0, tab));
    }
  }
#if 0
  // Unnecessary if it's confirmed Essential=>Required
  {
    DpkgQuery q{DpkgQueryFormat("Essential")};
    for (string::size_type tab; (tab = q.GetLine(s));) {
      const string_view attr = string_view{s}.substr(tab + 1);
      if (attr != "yes") {
        continue;
      }
      roots.insert(s.substr(0, tab));
    }
  }
#endif
  return roots;
}

void CompleteDependencies(set<string> &roots) {
  string s;
  s.reserve(100);
  set<string> result;
  vector<const char *> args{"/usr/bin/apt-cache", "depends", "-i", "--recurse"};
  args.reserve(3 * roots.size());
  for (auto &root : roots) {
    args.emplace_back(result.emplace(std::move(root)).first->c_str());
  }
  args.emplace_back(nullptr);
  {
    AptCache q{const_cast<char *const *>(&args[0])};
    while (q.GetLine(s)) {
      const auto dep_{string_view{s}.substr(AptCache::PREFIX.size())};
      if (dep_[0] == '<')
        continue; // Virtual packages usually aren't essential
      const size_t sep{dep_.find(':')};
      const auto dep{!~sep ? dep_ : dep_.substr(0, sep)};
      if (dep.empty())
        abort();
      result.emplace(dep);
    }
  }
  roots = std::move(result);
}

void CollectPackagePaths(int fd, set<string> &result,
                         const Trie &exclude_paths) noexcept {
  __gnu_cxx::stdio_filebuf<char> file{fd, ios::in};
  istream is{&file};
  string s{};
  for (;;) {
    do {
      if (!getline(is, s))
        return;
    } while (s.empty());
    if (s[0] != '/')
      abort();
    if (s[1] == '.')
      continue;
    if (s.ends_with('.'))
      abort();
    if (exclude_paths.HasPath(string_view{s}.substr(1)))
      continue;
    result.emplace(std::move(s));
  }
}

void CollectPackagesPaths(const set<string> &packages, set<string> &result,
                          const Trie &exclude_paths) noexcept {
  string basename{};
  basename.reserve(100);

  const int dir = open("/var/lib/dpkg/info", O_CLOEXEC | O_DIRECTORY | O_PATH);
  if (dir < 0)
    abort();

  static constexpr string_view PKG_UNI_SUFFIX = ".list";
  static constexpr size_t PKG_UNI_SUFFIX_SIZE = PKG_UNI_SUFFIX.length();
  static constexpr string_view PKG_32_SUFFIX = ":i386.list";
  static constexpr size_t PKG_32_SUFFIX_SIZE = PKG_32_SUFFIX.length();
  static constexpr string_view PKG_64_SUFFIX = ":amd64.list";

  for (const string &package : packages) {
    basename.clear();
    basename.append(package);
    basename.append(PKG_UNI_SUFFIX);
#define OpenInfo(x) openat(dir, x.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)
    int fd = OpenInfo(basename);
    if (fd >= 0) {
      CollectPackagePaths(fd, result, exclude_paths);
    }
    basename.erase(basename.length() - PKG_UNI_SUFFIX_SIZE);
    basename.append(PKG_32_SUFFIX);
    fd = OpenInfo(basename);
    if (fd >= 0) {
      CollectPackagePaths(fd, result, exclude_paths);
    }
    basename.erase(basename.length() - PKG_32_SUFFIX_SIZE);
    basename.append(PKG_64_SUFFIX);
    fd = OpenInfo(basename);
    if (fd >= 0) {
      CollectPackagePaths(fd, result, exclude_paths);
    }
#undef OpenInfo
  }

  if (close(dir))
    abort();
}

void CollectIncludeDirs(const vector<string> &include_dirs,
                        set<string> &result) {
  for (const string &root : include_dirs) {
    for (const filesystem::directory_entry &entry :
         filesystem::recursive_directory_iterator(
             root, filesystem::directory_options::skip_permission_denied)) {
      result.emplace(entry.path().c_str());
    }
  }
}

void OutputPath(const string &path, ofstream &bom) {
  assert(path[0] == '/');
  struct stat st {};
  if (lstat(path.c_str(), &st))
    return;

  mode_t mode = st.st_mode;
  char type;
  if (S_ISREG(mode)) {
    type = (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) ? COPY_EXE : COPY_DAT;
  } else if (S_ISDIR(mode)) {
    type = COPY_DIR;
  } else if (S_ISLNK(mode)) {
    type = COPY_LNK;
  } else {
    return;
  }

  bom << type << path << '\n';
}

void Run() {
  const Options options{LoadCustomFileList()};
  set<string> paths{};
  set<string> packages = LoadDpkgNecessary();
  packages.insert(options.include_pkgs.begin(), options.include_pkgs.end());
  CompleteDependencies(packages);
  CollectPackagesPaths(packages, paths, options.exclude_paths);
  CollectIncludeDirs(options.include_dirs, paths);
  ofstream bom{WORK_FILE_NAME};
  for (auto &s : paths) {
    OutputPath(s, bom);
  }
}
} // namespace

int main() {
  if (getuid()) {
    puts("Warning: without root some restricted files may be skipped");
  }
  if (!filesystem::is_directory("config")) {
    puts("Configuration is missing. Consider copying config_example to config");
    return 1;
  }
  if (!filesystem::is_directory("/var/lib/dpkg")) {
    puts("Expected a Debian-like system with /var/lib/dpkg");
    return 1;
  }
  Run();
  return 0;
}
