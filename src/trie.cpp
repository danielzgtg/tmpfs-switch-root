#include "trie.h"
#include <cassert>
#include <algorithm>

using namespace std;

static pair<string_view, string_view> SplitOneDirname(string_view path) {
  const size_t sep = path.find('/');
  if (!sep)
    abort();
  const pair<string_view, string_view> result =
      ~sep ? make_pair(path.substr(0, sep), path.substr(sep + 1))
           : make_pair(path, string_view{});
  if (result.first.empty())
    abort();
  return result;
}

pair<reference_wrapper<const Trie>, string_view>
Trie::FindPath(string_view path) const { // NOLINT(misc-no-recursion)
  if (path.empty() || !child) {
    return {ref(*this), {}};
  }
  const auto [cur, next] = SplitOneDirname(path);
  const vector<Trie> &nodes = *child;
  // Linear Search for small N
  const auto it =
      std::find_if(nodes.begin(), nodes.end(),
                   [cur = cur](const auto &x) { return x.filename == cur; });
  if (it == nodes.end()) {
    return {cref(*this), path};
  }
  const Trie &node = *it;
  return node.FindPath(next);
}

pair<reference_wrapper<Trie>, string_view> Trie::FindPath(string_view path) {
  const auto [sub, next] = const_cast<const Trie *>(this)->FindPath(path);
  return {ref(const_cast<Trie &>(sub.get())), next};
}

bool Trie::HasPath(string_view path) const {
  return FindPath(path).second.empty();
}

void Trie::AddPath(string_view path) {
  auto [base, rem] = FindPath(path);
  unique_ptr<vector<Trie>> *tree = &base.get().child;
  if (!rem.empty()) {
    assert(*tree);
    for (;;) {
      const auto [cur, next] = SplitOneDirname(rem);
      tree = &(*tree)->emplace_back(cur).child;
      if (next.empty())
        break;
      *tree = make_unique<vector<Trie>>();
      rem = next;
    }
  }
  tree->reset();
}
