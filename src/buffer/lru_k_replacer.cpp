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
#include <iostream>
#include <iterator>
#include <mutex>
#include <ostream>
#include <vector>
#include "common/exception.h"
#include "common/macros.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  // 加锁
  std::lock_guard<std::mutex> access_lock(latch_);
  bool find = false;
  // k距离为无穷的页id
  std::vector<size_t> inf_k_d_frame_id;
  // 记录最大的k距离
  size_t max_k_d = 0;

  // 记录最大的k距离页id
  frame_id_t max_k_d_id = replacer_size_;
  //   遍历存储的node
  for (auto &node : node_store_) {
    // 该节点是否可淘汰
    if (!node.second.GetEvictable()) {
      continue;
    }
    size_t tmp = node.second.GetKDistance(current_timestamp_);
    // 如果当前节点的k距离更大，更新页id
    if (tmp > max_k_d) {
      find = true;
      max_k_d = tmp;
      max_k_d_id = node.first;
    }

    // 如果k距离为无穷
    if (tmp == UINT32_MAX) {
      inf_k_d_frame_id.push_back(node.first);
    }
  }

  *frame_id = max_k_d_id;

  //   如果有inf
  size_t min_k_d = UINT32_MAX;
  for (size_t &i : inf_k_d_frame_id) {
    // 可淘汰且最近访问时间小
    if (node_store_[i].GetBackAccess() < min_k_d) {
      min_k_d = node_store_[i].GetBackAccess();
      *frame_id = i;
    }
  }
  // 清空历史记录
  if (find) {
    node_store_[*frame_id].RemoveHistory();
    node_store_[*frame_id].SetEvictable(false);
    curr_size_--;
    // node_store_.erase(*frame_id);
    return true;
  }

  return false;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  // 加锁
  std::lock_guard<std::mutex> access_lock(latch_);
  // 如果页id不合法
  if (static_cast<size_t>(frame_id) > replacer_size_) {
    throw Exception("LRUKReplacer::RecordAccess: frame_id is invalid");
  }
  if (node_store_.find(frame_id) == node_store_.end()) {
    // 节点已满
    if (node_store_.size() == replacer_size_) {
      return;
    }
    // 如果不存在，则添加
    node_store_[frame_id] = LRUKNode(frame_id, k_);
  }
  // 给当前页添加访问历史
  node_store_[frame_id].AddHistory(current_timestamp_);
  // 时间自增
  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  // 加锁
  std::lock_guard<std::mutex> access_lock(latch_);
  // 如果页id不合法
  if (node_store_.find(frame_id) == node_store_.end()) {
    throw Exception("LRUKReplacer::RecordAccess: frame_id is invalid");
  }
  if (!set_evictable && node_store_[frame_id].GetEvictable()) {
    curr_size_--;
  } else if (set_evictable && !node_store_[frame_id].GetEvictable()) {
    curr_size_++;
  }
  node_store_[frame_id].SetEvictable(set_evictable);
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  // 加锁
  std::lock_guard<std::mutex> access_lock(latch_);
  if (node_store_.find(frame_id) == node_store_.end()) {
    return;
  }
  // 如果页id不合法或不可驱逐
  if (!node_store_[frame_id].GetEvictable()) {
    throw Exception("LRUKReplacer::Remove: frame_id can not be remove");
  }
  //   移除
  node_store_[frame_id].RemoveHistory();
  node_store_[frame_id].SetEvictable(false);
  node_store_.erase(frame_id);
  curr_size_--;
}

auto LRUKReplacer::Size() -> size_t {
  std::lock_guard lock_latch(latch_);
  return curr_size_;
}

}  // namespace bustub