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
#include <cstddef>
#include <limits>
#include <mutex>
#include <utility>
#include "common/exception.h"

namespace bustub {

LRUKNode::LRUKNode(size_t k) : k_(k) {}

auto LRUKNode::IsEvictable() const -> bool { return is_evictable_; }

void LRUKNode::SetEvictable(bool is_evictable) { is_evictable_ = is_evictable; }

auto LRUKNode::GetCount() const -> size_t { return history_.size(); }

auto LRUKNode::GetEarliestTimestamp() const -> size_t { return history_.front(); }

void LRUKNode::RecordAccess(size_t timestamp) {
  history_.push_back(timestamp);
  if (GetCount() > k_) {
    history_.pop_front();
  }
}

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *id) -> bool {
  std::lock_guard lock(latch_);

  bool have_inf = false;
  bool evict_success = false;
  size_t min_timestamp = std::numeric_limits<size_t>::max();
  for (auto &[frame_id, node] : node_store_) {
    if (!node.IsEvictable()) {
      continue;
    }
    evict_success = true;
    if (node.GetCount() == 0) {
      *id = frame_id;
      break;
    }
    if (node.GetCount() == k_) {
      if (!have_inf && node.GetEarliestTimestamp() < min_timestamp) {
        min_timestamp = node.GetEarliestTimestamp();
        *id = frame_id;
      }
    } else {
      if (have_inf) {
        if (node.GetEarliestTimestamp() < min_timestamp) {
          min_timestamp = node.GetEarliestTimestamp();
          *id = frame_id;
        }
      } else {
        min_timestamp = node.GetEarliestTimestamp();
        *id = frame_id;
        have_inf = true;
      }
    }
  }

  if (evict_success) {
    curr_size_--;
    node_store_.erase(*id);
  }

  return evict_success;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, AccessType access_type) {
  if (static_cast<size_t>(frame_id) > replacer_size_) {
    throw Exception("LRUKReplacer::RecordAccess: frame_id is invalid");
  }

  std::lock_guard lock(latch_);

  auto iter = node_store_.find(frame_id);
  if (iter == node_store_.end()) {
    auto node = LRUKNode(k_);
    if (access_type != AccessType::Scan) {
      node.RecordAccess(current_timestamp_++);
    }
    node_store_.emplace(frame_id, node);
  } else if (access_type != AccessType::Scan) {
    auto &node = iter->second;
    node.RecordAccess(current_timestamp_++);
  }
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  if (static_cast<size_t>(frame_id) > replacer_size_) {
    throw Exception("LRUKReplacer::SetEvictable: frame_id is invalid");
  }

  std::lock_guard lock(latch_);

  auto iter = node_store_.find(frame_id);
  if (iter == node_store_.end()) {
    throw Exception("LRUKReplacer::SetEvictable: frame_id is invalid");
  }
  auto &node = iter->second;
  if (set_evictable) {
    if (!node.IsEvictable()) {
      node.SetEvictable(set_evictable);
      curr_size_++;
    }
  } else if (node.IsEvictable()) {
    node.SetEvictable(set_evictable);
    curr_size_--;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::lock_guard lock(latch_);

  auto iter = node_store_.find(frame_id);
  if (iter == node_store_.end()) {
    return;
  }

  auto &node = iter->second;
  if (!node.IsEvictable()) {
    throw Exception("LRUKReplacer::Remove: frame_id can not be remove");
  }
  node_store_.erase(frame_id);
  curr_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::lock_guard lock(latch_);
  return curr_size_;
}

}  // namespace bustub