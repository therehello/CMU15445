//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// extendible_htable_bucket_page.cpp
//
// Identification: src/storage/page/extendible_htable_bucket_page.cpp
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>
#include "storage/index/int_comparator.h"

#include "storage/page/extendible_htable_bucket_page.h"

namespace bustub {

template <typename K, typename V, typename KC>
void ExtendibleHTableBucketPage<K, V, KC>::Init(uint32_t max_size) {
  max_size_ = max_size;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::Lookup(const K &key, V &value, const KC &cmp) const -> bool {
  auto idx = KeyIndex(key, cmp);
  if (idx >= size_ || cmp(KeyAt(idx), key)) {
    return false;
  }
  value = ValueAt(idx);
  return true;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::KeyIndex(const K &key, const KC &cmp) const -> uint32_t {
  auto pos = std::lower_bound(array_, array_ + Size(), key, [&cmp](auto &a, auto &b) { return cmp(a.first, b) < 0; });
  return pos - array_;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::Insert(const K &key, const V &value, const KC &cmp) -> bool {
  if (IsFull()) {
    return false;
  }
  auto idx = KeyIndex(key, cmp);
  if (idx < size_) {
    if (cmp(KeyAt(idx), key) == 0) {
      return false;
    }
    std::move_backward(array_ + idx, array_ + size_, array_ + size_ + 1);
  }
  array_[idx] = {key, value};
  size_++;
  return true;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::Remove(const K &key, const KC &cmp) -> bool {
  auto idx = KeyIndex(key, cmp);
  if (idx >= size_ || cmp(KeyAt(idx), key)) {
    return false;
  }
  RemoveAt(idx);
  return true;
}

template <typename K, typename V, typename KC>
void ExtendibleHTableBucketPage<K, V, KC>::Remove(const std::vector<uint32_t> &remove_idx) {
  assert(remove_idx.size() <= size_);
  for (uint32_t i = 0, j = 0, k = 0; i < size_; i++) {
    if (j < remove_idx.size()) {
      if (i != remove_idx[j]) {
        array_[k++] = array_[i];
      } else {
        j++;
      }
    } else {
      array_[k++] = array_[i];
    }
  }
  size_ -= remove_idx.size();
}

template <typename K, typename V, typename KC>
void ExtendibleHTableBucketPage<K, V, KC>::RemoveAt(uint32_t idx) {
  assert(0 <= idx && idx < size_);
  std::move(array_ + idx + 1, array_ + size_, array_ + idx);
  size_--;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::KeyAt(uint32_t idx) const -> K {
  assert(0 <= idx && idx < size_);
  return array_[idx].first;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::ValueAt(uint32_t idx) const -> V {
  assert(0 <= idx && idx < size_);
  return array_[idx].second;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::EntryAt(uint32_t idx) const -> const std::pair<K, V> & {
  assert(0 <= idx && idx < size_);
  return array_[idx];
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::Size() const -> uint32_t {
  return size_;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::IsFull() const -> bool {
  return size_ == max_size_;
}

template <typename K, typename V, typename KC>
auto ExtendibleHTableBucketPage<K, V, KC>::IsEmpty() const -> bool {
  return size_ == 0;
}

template class ExtendibleHTableBucketPage<int, int, IntComparator>;
template class ExtendibleHTableBucketPage<GenericKey<4>, RID, GenericComparator<4>>;
template class ExtendibleHTableBucketPage<GenericKey<8>, RID, GenericComparator<8>>;
template class ExtendibleHTableBucketPage<GenericKey<16>, RID, GenericComparator<16>>;
template class ExtendibleHTableBucketPage<GenericKey<32>, RID, GenericComparator<32>>;
template class ExtendibleHTableBucketPage<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub