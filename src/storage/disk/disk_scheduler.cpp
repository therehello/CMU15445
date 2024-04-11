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
#include <utility>
#include "storage/disk/disk_manager.h"

namespace bustub {

DiskScheduler::DiskScheduler(DiskManager *disk_manager) : disk_manager_(disk_manager) {}

auto DiskScheduler::Schedule(DiskRequest r) -> std::future<void> {
  return threadpool_.Submit([this, r = std::move(r)]() mutable {
    if (r.is_write_) {
      disk_manager_->WritePage(r.page_id_, r.data_);
    } else {
      disk_manager_->ReadPage(r.page_id_, r.data_);
    }
    r.callback_.set_value(true);
  });
}

}  // namespace bustub
