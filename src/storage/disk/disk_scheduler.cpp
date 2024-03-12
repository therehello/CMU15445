//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.cpp
//
// Identification: src/storage/disk/disk_scheduler.cpp
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_scheduler.h"
#include <cstddef>
#include <optional>
#include <thread>
#include <utility>
#include "storage/disk/disk_manager.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {
  // TODO(P1): remove this line after you have implemented the disk scheduler API

  for (int i = 0; i < 16; i++) {
    background_threads_[i] = std::thread([this, i] { StartWorkerThread(request_queues_[i]); });
  }
}

DiskScheduler::~DiskScheduler() {
  // Put a `std::nullopt` in the queue to signal to exit the loop
  for (auto &request_queue : request_queues_) {
    request_queue.Put(std::nullopt);
  }
  for (auto &background_thread : background_threads_) {
    background_thread.join();
  }
}

void DiskScheduler::Schedule(DiskRequest r) {
  size_t idx = r.page_id_ % 16;
  request_queues_[idx].Put(std::move(r));
}

void DiskScheduler::StartWorkerThread(Channel<std::optional<DiskRequest>> &request_queue) {
  std::optional<DiskRequest> r;
  while ((r = request_queue.Get())) {
    if (r->is_write_) {
      disk_manager_->WritePage(r->page_id_, r->data_);
    } else {
      disk_manager_->ReadPage(r->page_id_, r->data_);
    }
    r->callback_.set_value(true);
  }
}

}  // namespace bustub
