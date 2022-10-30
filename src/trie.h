#pragma once

#include <memory>
#include <string>
#include <vector>

class Trie {
  std::string filename;
  std::unique_ptr<std::vector<Trie>> child;

  [[nodiscard]] std::pair<std::reference_wrapper<const Trie>, std::string_view>
  FindPath(std::string_view path) const;

  [[nodiscard]] std::pair<std::reference_wrapper<Trie>, std::string_view>
  FindPath(std::string_view path);

public:
  Trie() : filename{}, child{std::make_unique<std::vector<Trie>>()} {}
  explicit Trie(const std::string_view filename_) : filename{filename_}, child{} {}

  Trie(const Trie &) = delete;
  Trie &operator=(const Trie &) = delete;
  Trie(Trie &&) = default;
  Trie &operator=(Trie &&) = default;

  [[nodiscard]] bool HasPath(std::string_view path) const;

  void AddPath(std::string_view path);
};
