#include "primer/trie.h"
#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include "common/exception.h"

namespace bustub {

template <class T>
auto Trie::Get(std::string_view key) const -> const T * {
  if (root_ == nullptr) {
    return nullptr;
  }
  auto curr = root_;
  for (auto &ch : key) {
    try {
      curr = curr->children_.at(ch);
    } catch (std::out_of_range) {
      return nullptr;
    }
  }
  auto node = dynamic_cast<const TrieNodeWithValue<T> *>(curr.get());
  if (node) {
    return node->value_.get();
  }
  return nullptr;

  // You should walk through the trie to find the node corresponding to the key. If the node doesn't exist, return
  // nullptr. After you find the node, you should use `dynamic_cast` to cast it to `const TrieNodeWithValue<T> *`. If
  // dynamic_cast returns `nullptr`, it means the type of the value is mismatched, and you should return nullptr.
  // Otherwise, return the value.
}

template <class T>
auto Trie::Put(std::string_view key, T value) const -> Trie {
  if (key.empty()) {
    std::shared_ptr<TrieNode> new_trie{nullptr};
    if (root_) {
      new_trie = std::make_shared<TrieNodeWithValue<T>>(root_->children_, std::make_shared<T>(std::move(value)));
    } else {
      new_trie = std::make_shared<TrieNodeWithValue<T>>(std::make_shared<T>(std::move(value)));
    }
    return Trie(new_trie);
  }
  auto new_trie = root_ ? root_->Clone() : std::make_shared<TrieNode>();
  auto curr = new_trie;
  for (auto &ch : key) {
    auto i = curr->children_.find(ch);
    if (i != curr->children_.end()) {
      if (&ch != &key.back()) {
        curr = i->second->Clone();
      } else {
        curr = std::make_shared<TrieNodeWithValue<T>>(i->second->children_, std::make_shared<T>(std::move(value)));
      }
      i->second = curr;
    } else {
      std::shared_ptr<TrieNode> new_node{nullptr};
      if (&ch != &key.back()) {
        new_node = std::make_shared<TrieNode>();
      } else {
        new_node = std::make_shared<TrieNodeWithValue<T>>(std::make_shared<T>(std::move(value)));
      }
      curr->children_[ch] = new_node;
      curr = new_node;
    }
  }
  return Trie(new_trie);

  // Note that `T` might be a non-copyable type. Always use `std::move` when creating `shared_ptr` on that value.

  // You should walk through the trie and create new nodes if necessary. If the node corresponding to the key already
  // exists, you should create a new `TrieNodeWithValue`.
}

auto Trie::Remove(std::string_view key) const -> Trie {
  if (root_ == nullptr) {
    return *this;
  }
  auto new_root = std::shared_ptr<TrieNode>(root_->Clone());
  if (key.empty()) {
    if (new_root->children_.empty()) {
      new_root = nullptr;
    } else {
      new_root = std::make_shared<TrieNode>(new_root->children_);
    }
    return Trie(move(new_root));
  }
  auto curr = new_root;
  std::vector<std::pair<std::shared_ptr<TrieNode>, char>> nodes;
  for (auto &ch : key) {
    auto it = curr->children_.find(ch);
    if (it == curr->children_.end()) {
      return Trie(move(new_root));
    }
    nodes.emplace_back(curr, ch);
    curr = it->second->Clone();
    it->second = curr;
  }
  nodes.back().first->children_[nodes.back().second] = std::make_shared<TrieNode>(curr->children_);
  if (!curr->children_.empty()) {
    return Trie(move(new_root));
  }
  for (auto it = nodes.rbegin(); it != nodes.rend(); it++) {
    auto &[node, ch] = *it;
    node->children_.erase(ch);
    if (node->is_value_node_ || !node->children_.empty()) {
      return Trie(move(new_root));
    }
  }
  return Trie(nullptr);

  // You should walk through the trie and remove nodes if necessary. If the node doesn't contain a value any more,
  // you should convert it to `TrieNode`. If a node doesn't have children any more, you should remove it.
}

// Below are explicit instantiation of template functions.
//
// Generally people would write the implementation of template classes and functions in the header file. However, we
// separate the implementation into a .cpp file to make things clearer. In order to make the compiler know the
// implementation of the template functions, we need to explicitly instantiate them here, so that they can be picked up
// by the linker.

template auto Trie::Put(std::string_view key, uint32_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint32_t *;

template auto Trie::Put(std::string_view key, uint64_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint64_t *;

template auto Trie::Put(std::string_view key, std::string value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const std::string *;

// If your solution cannot compile for non-copy tests, you can remove the below lines to get partial score.

using Integer = std::unique_ptr<uint32_t>;

template auto Trie::Put(std::string_view key, Integer value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const Integer *;

template auto Trie::Put(std::string_view key, MoveBlocked value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const MoveBlocked *;

}  // namespace bustub
