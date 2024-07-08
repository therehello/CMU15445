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
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include "common/config.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size),
      pages_(new Page[pool_size]),
      disk_scheduler_(std::make_unique<DiskScheduler>(disk_manager)),
      log_manager_(log_manager),
      replacer_(std::make_unique<LRUKReplacer>(pool_size, replacer_k)),
      avaliable_(pool_size, true),
      cond_(pool_size) {
  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::unique_lock lock_latch(latch_);

  frame_id_t frame_id = -1;
  if (free_list_.empty()) {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  } else {
    frame_id = free_list_.front();
    free_list_.pop_front();
  }

  auto page = pages_ + frame_id;
  page->pin_count_ = 0;
  page_table_.erase(page->GetPageId());

  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  *page_id = AllocatePage();
  page->pin_count_++;
  page_table_[*page_id] = frame_id;

  if (page->IsDirty()) {
    WritePage(frame_id);
  }
  page->ResetMemory();

  page->page_id_ = *page_id;

  return page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id, AccessType access_type) -> Page * {
  std::unique_lock lock_latch(latch_);

  if (auto iter = page_table_.find(page_id); iter != page_table_.end()) {
    auto frame_id = iter->second;
    auto page = pages_ + frame_id;
    page->pin_count_++;
    replacer_->RecordAccess(frame_id, access_type);
    replacer_->SetEvictable(frame_id, false);
    if (!avaliable_[frame_id]) {
      cond_[frame_id].wait(lock_latch, [this, frame_id] { return avaliable_[frame_id]; });
    }
    return page;
  }

  frame_id_t frame_id = -1;
  if (free_list_.empty()) {
    if (!replacer_->Evict(&frame_id)) {
      return nullptr;
    }
  } else {
    frame_id = free_list_.front();
    free_list_.pop_front();
  }

  auto page = pages_ + frame_id;
  page->pin_count_ = 0;
  page_table_.erase(page->GetPageId());

  page->pin_count_++;
  /** 在写脏页之前提前设置好是为了阻塞请求相同页的其他线程  */
  page_table_[page_id] = frame_id;

  replacer_->RecordAccess(frame_id, access_type);
  replacer_->SetEvictable(frame_id, false);

  if (page->IsDirty()) {
    /** 这里就先设置好，避免释放锁的时候，请求相同页的其他线程获取到 page */
    avaliable_[frame_id] = false;
    WritePage(frame_id);
  }

  page->page_id_ = page_id;
  ReadPage(frame_id);

  return page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  std::lock_guard lock_latch(latch_);

  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    return false;
  }

  auto frame_id = iter->second;
  auto page = pages_ + frame_id;
  if (page->GetPinCount() == 0) {
    return false;
  }

  if (is_dirty) {
    page->is_dirty_ = is_dirty;
  }
  if (--page->pin_count_ == 0) {
    replacer_->SetEvictable(frame_id, true);
  }

  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  if (page_id == INVALID_PAGE_ID) {
    return false;
  }

  std::lock_guard lock_latch(latch_);

  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    return false;
  }

  auto frame_id = iter->second;
  auto page = pages_ + frame_id;
  if (page->IsDirty()) {
    WritePage(frame_id);
  }

  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard lock_latch(latch_);

  for (auto &[page_id, frame_id] : page_table_) {
    auto page = pages_ + frame_id;
    if (page->IsDirty()) {
      WritePage(frame_id);
    }
  }
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::unique_lock lock_latch(latch_);

  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    return true;
  }

  auto frame_id = iter->second;
  auto page = pages_ + frame_id;
  if (page->GetPinCount() > 0) {
    return false;
  }

  page->pin_count_ = 0;
  page_table_.erase(iter);

  free_list_.push_back(frame_id);
  replacer_->Remove(frame_id);

  if (page->IsDirty()) {
    WritePage(frame_id);
  }

  DeallocatePage(page_id);

  return true;
}

void BufferPoolManager::WritePage(frame_id_t frame_id) {
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  auto page = pages_ + frame_id;
  disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId(), std::move(promise)});
  latch_.unlock();
  future.get();
  latch_.lock();
  page->is_dirty_ = false;
}

void BufferPoolManager::ReadPage(frame_id_t frame_id) {
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  auto page = pages_ + frame_id;
  disk_scheduler_->Schedule({false, page->GetData(), page->GetPageId(), std::move(promise)});
  avaliable_[frame_id] = false;
  latch_.unlock();
  future.get();
  latch_.lock();
  avaliable_[frame_id] = true;
  cond_[frame_id].notify_all();
}

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard { return {this, FetchPage(page_id)}; }

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard {
  auto page = FetchPage(page_id);
  if (page != nullptr) {
    page->RLatch();
  }
  return {this, page};
}

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard {
  auto page = FetchPage(page_id);
  if (page != nullptr) {
    page->WLatch();
  }
  return {this, page};
}

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard { return {this, NewPage(page_id)}; }

}  // namespace bustub