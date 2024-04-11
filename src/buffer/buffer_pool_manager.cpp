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
#include <emmintrin.h>
#include <unistd.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "common/config.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

//! Class that implements exponential backoff.
class AtomicBackoff {
  //! Time delay, in units of "pause" instructions.
  /** Should be equal to approximately the number of "pause" instructions
    that take the same time as an context switch. Must be a power of two.*/
  static constexpr std::int32_t LOOPS_BEFORE_YIELD = 16;
  std::int32_t count_;

 public:
  // In many cases, an object of this type is initialized eagerly on hot path,
  // as in for(atomic_backoff b; ; b.pause()) { /*loop body*/ }
  // For this reason, the construction cost must be very small!
  AtomicBackoff() : count_(1) {}
  // This constructor pauses immediately; do not use on hot paths!
  explicit AtomicBackoff(bool /*unused*/) : count_(1) { Pause(); }

  //! No Copy
  AtomicBackoff(const AtomicBackoff &) = delete;
  auto operator=(const AtomicBackoff &) -> AtomicBackoff & = delete;

  //! Pause for a while.
  void Pause() {
    if (count_ <= LOOPS_BEFORE_YIELD) {
      for (int delay = 0; delay < count_; delay++) {
        _mm_pause();
      }
      // Pause twice as long the next time.
      count_ *= 2;
    } else {
      // Pause is so long that we might as well yield CPU to scheduler.
      std::this_thread::yield();
    }
  }
};

//! Spin WHILE the condition is true.
/** T and U should be comparable types. */
template <typename T, typename C>
auto SpinWaitWhile(const std::atomic<T> &location, C comp, std::memory_order order) -> T {
  AtomicBackoff backoff;
  T snapshot = location.load(order);
  while (comp(snapshot)) {
    backoff.Pause();
    snapshot = location.load(order);
  }
  return snapshot;
}

//! Spin WHILE the value of the variable is equal to a given value
/** T and U should be comparable types. */
template <typename T, typename U>
auto SpinWaitWhileEq(const std::atomic<T> &location, const U value, std::memory_order order = std::memory_order_acquire)
    -> T {
  return SpinWaitWhile(
      location, [&value](T t) { return t == value; }, order);
}

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size),
      pages_(new Page[pool_size]),
      disk_scheduler_(std::make_unique<DiskScheduler>(disk_manager)),
      log_manager_(log_manager),
      replacer_(std::make_unique<LRUKReplacer>(pool_size, replacer_k)),
      avaliable_(pool_size) {
  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  frame_id_t frame_id;
  Page *page;

  {
    std::lock_guard lock(latch_);

    if (free_list_.empty()) [[likely]] {
      if (!replacer_->Evict(&frame_id)) {
        return nullptr;
      }
    } else {
      frame_id = free_list_.front();
      free_list_.pop_front();
    }

    page = pages_ + frame_id;
    page_table_.erase(page->GetPageId());

    *page_id = AllocatePage();
    page_table_[*page_id] = frame_id;
    avaliable_[frame_id].store(false, std::memory_order_release);
  }

  std::future<void> future_write;
  if (page->IsDirty()) {
    future_write = disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId()});
  }

  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  page->pin_count_ = 1;
  page->is_dirty_ = false;
  page->page_id_ = *page_id;
  if (future_write.valid()) {
    future_write.get();
  }
  page->ResetMemory();
  avaliable_[frame_id].store(true, std::memory_order_release);

  return page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id, AccessType access_type) -> Page * {
  frame_id_t frame_id;
  Page *page;

  {
    std::unique_lock lock(latch_);

    if (auto iter = page_table_.find(page_id); iter != page_table_.end()) {
      frame_id = iter->second;
      page = pages_ + frame_id;
      page->pin_count_++;
      replacer_->RecordAccess(frame_id, access_type);
      replacer_->SetEvictable(frame_id, false);
      lock.unlock();
      if (!avaliable_[frame_id].load(std::memory_order_acquire)) [[unlikely]] {
        SpinWaitWhileEq(avaliable_[frame_id], false);
      }
      return page;
    }

    if (free_list_.empty()) [[likely]] {
      if (!replacer_->Evict(&frame_id)) {
        return nullptr;
      }
    } else {
      frame_id = free_list_.front();
      free_list_.pop_front();
    }

    page = pages_ + frame_id;
    page_table_.erase(page->GetPageId());
    avaliable_[frame_id].store(false, std::memory_order_release);
    /** 在写脏页之前提前设置好是为了阻塞请求相同页的其他线程  */
    page_table_[page_id] = frame_id;
    page->pin_count_ = 1;
  }

  std::future<void> future_write;
  if (page->IsDirty()) {
    future_write = disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId()});
  }
  replacer_->RecordAccess(frame_id, access_type);
  replacer_->SetEvictable(frame_id, false);
  if (page->IsDirty()) {
    future_write.get();
    page->is_dirty_ = false;
  }
  page->page_id_ = page_id;
  auto future_read = disk_scheduler_->Schedule({false, page->GetData(), page->GetPageId()});
  future_read.get();
  avaliable_[frame_id].store(true, std::memory_order_release);

  return page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  std::lock_guard lock(latch_);

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

  std::lock_guard lock(latch_);

  auto iter = page_table_.find(page_id);
  if (iter == page_table_.end()) {
    return false;
  }

  auto frame_id = iter->second;
  auto page = pages_ + frame_id;
  if (page->IsDirty()) {
    disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId()}).get();
    page->is_dirty_ = false;
  }

  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::lock_guard lock(latch_);

  std::vector<std::future<void>> futures;
  for (auto &[page_id, frame_id] : page_table_) {
    auto page = pages_ + frame_id;
    if (page->IsDirty()) {
      futures.push_back(disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId()}));
      page->is_dirty_ = false;
    }
  }
  for (auto &future : futures) {
    future.get();
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
    disk_scheduler_->Schedule({true, page->GetData(), page->GetPageId()}).get();
    page->is_dirty_ = false;
  }

  DeallocatePage(page_id);

  return true;
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