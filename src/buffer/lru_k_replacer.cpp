//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include <cassert>
#include <cstddef>
#include <mutex>
#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard lock_latch(latch_);
  if (curr_size_ == 0) {
    return false;
  }
  for (auto i = lru_.rbegin(); i != lru_.rend(); i++) {
    auto p = node_store_.find(*i);
    if (p->second.is_evictable_) {
      *frame_id = *i;
      lru_.erase(prev(i.base()));
      node_store_.erase(p);
      curr_size_--;
      return true;
    }
  }
  for (auto i = lru_k_.rbegin(); i != lru_k_.rend(); i++) {
    auto p = node_store_.find(*i);
    if (p->second.is_evictable_) {
      *frame_id = *i;
      lru_k_.erase(prev(i.base()));
      node_store_.erase(p);
      curr_size_--;
      return true;
    }
  }
  assert(0);
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) <= replacer_size_,
                "LRUKReplacer::RecordAccess: frame_id is invalid");
  std::lock_guard lock_latch(latch_);
  auto p = node_store_.find(frame_id);
  if (p == node_store_.end()) {
    if (node_store_.size() == replacer_size_) {
      return;
    }
    lru_.push_front(frame_id);
    node_store_[frame_id] = LRUKNode(frame_id, lru_.begin());
  } else {
    auto &node = p->second;
    if (node.history_cnt_ >= k_) {
      lru_k_.erase(node.lru_map_);
    } else {
      lru_.erase(node.lru_map_);
    }
    ++node.history_cnt_;
    if (node.history_cnt_ >= k_) {
      lru_k_.push_front(node.frame_id_);
      node.lru_map_ = lru_k_.begin();
    } else {
      lru_.push_front(node.frame_id_);
      node.lru_map_ = lru_.begin();
    }
  }
  ++current_timestamp_;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  BUSTUB_ASSERT(frame_id >= 0 && static_cast<size_t>(frame_id) <= replacer_size_,
                "LRUKReplacer::RecordAccess: frame_id is invalid");
  std::lock_guard lock_latch(latch_);
  auto p = node_store_.find(frame_id);
  BUSTUB_ASSERT(p != node_store_.end(), "LRUKReplacer::SetEvictable: frame_id is invalid");
  if (p->second.is_evictable_ && !set_evictable) {
    curr_size_--;
    p->second.is_evictable_ = false;
  } else if (!p->second.is_evictable_ && set_evictable) {
    curr_size_++;
    p->second.is_evictable_ = true;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard lock_latch(latch_);
  auto p = node_store_.find(frame_id);
  if (p == node_store_.end()) {
    return;
  }
  BUSTUB_ASSERT(p->second.is_evictable_, "LRUKReplacer::Remove : frame_id can not be remove");
  if (p->second.history_cnt_ >= k_) {
    lru_k_.erase(p->second.lru_map_);
  } else {
    lru_.erase(p->second.lru_map_);
  }
  node_store_.erase(p);
}

auto LRUKReplacer::Size() -> size_t {
  std::lock_guard lock_latch(latch_);
  return curr_size_;
}

}  // namespace bustub