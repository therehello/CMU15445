//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include <algorithm>
#include <cstddef>
#include <mutex>
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_scheduler_(std::make_unique<DiskScheduler>(disk_manager)), log_manager_(log_manager) {
  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  frame_id_t frame_id = -1;
  std::lock_guard lock_latch(latch_);
  if (free_list_.empty()) {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  } else {
    frame_id = free_list_.back();
    free_list_.pop_back();
  }
  auto page = pages_ + frame_id;
  if (page->IsDirty()) {
    auto promise = disk_scheduler_->CreatePromise();
    auto future = promise.get_future();
    disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId(), std::move(promise)});
    future.get();
    page->is_dirty_ = false;
  }
  *page_id = AllocatePage();
  page_table_.erase(page->GetPageId());
  page_table_[*page_id] = frame_id;
  page->page_id_ = *page_id;
  page->pin_count_ = 1;
  page->ResetMemory();
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  return page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  std::lock_guard lock_latch(latch_);
  auto p = page_table_.find(page_id);
  if (p != page_table_.end()) {
    auto frame_id = p->second;
    auto page = pages_ + frame_id;
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    page->pin_count_++;
    return page;
  }
  frame_id_t frame_id = -1;
  if (free_list_.empty()) {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  } else {
    frame_id = free_list_.back();
    free_list_.pop_back();
  }
  auto page = pages_ + frame_id;
  if (page->IsDirty()) {
    auto promise = disk_scheduler_->CreatePromise();
    auto future = promise.get_future();
    disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId(), std::move(promise)});
    future.get();
    page->is_dirty_ = false;
  }
  page_table_.erase(page->GetPageId());
  page_table_[page_id] = frame_id;
  page->page_id_ = page_id;
  page->pin_count_ = 1;
  page->ResetMemory();
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  disk_scheduler_->Schedule({false, page->GetData(), page_id, std::move(promise)});
  future.get();
  return page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  std::lock_guard lock_latch(latch_);
  auto p = page_table_.find(page_id);
  if (p == page_table_.end()) {
    return false;
  }
  auto frame_id = p->second;
  auto page = pages_ + frame_id;
  if (is_dirty) {
    page->is_dirty_ = is_dirty;
  }
  if (page->GetPinCount() == 0) {
    return false;
  }
  page->pin_count_--;
  if (page->pin_count_ == 0) {
    replacer_->SetEvictable(frame_id, true);
  }
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  if (page_id == INVALID_PAGE_ID) {
    return false;
  }
  std::lock_guard lock_latch(latch_);
  auto p = page_table_.find(page_id);
  if (p == page_table_.end()) {
    return false;
  }
  auto frame_id = p->second;
  auto page = pages_ + frame_id;
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  disk_scheduler_->Schedule({true, page->GetData(), page_id, std::move(promise)});
  future.get();
  page->is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard lock_latch(latch_);
  auto page = pages_;
  for (size_t i = 0; i < pool_size_; i++, page++) {
    if (page->GetPageId() == INVALID_PAGE_ID) {
      continue;
    }
    auto promise = disk_scheduler_->CreatePromise();
    auto future = promise.get_future();
    disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId(), std::move(promise)});
    future.get();
    page->is_dirty_ = false;
  }
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::lock_guard lock_latch(latch_);
  auto p = page_table_.find(page_id);
  if (p == page_table_.end()) {
    return true;
  }
  auto frame_id = p->second;
  auto page = pages_ + frame_id;
  if (page->GetPinCount() > 0) {
    return false;
  }
  if (page->IsDirty()) {
    auto promise = disk_scheduler_->CreatePromise();
    auto future = promise.get_future();
    disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId(), std::move(promise)});
    future.get();
    page->is_dirty_ = false;
  }
  page_table_.erase(p);
  free_list_.push_back(frame_id);
  replacer_->Remove(frame_id);
  page->ResetMemory();
  page->page_id_ = INVALID_PAGE_ID;
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard { return {this, nullptr}; }

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard { return {this, nullptr}; }

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard { return {this, nullptr}; }

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard { return {this, nullptr}; }

}  // namespace bustub
