//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.h
//
// Identification: src/include/storage/disk/disk_scheduler.h
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <functional>
#include <future>  // NOLINT
#include <queue>
#include <thread>  // NOLINT
#include <tuple>
#include <vector>

#include "storage/disk/disk_manager.h"

namespace bustub {

class ThreadPool {
 public:
  explicit ThreadPool(size_t threads) {
    for (size_t i = 0; i < threads; ++i) {
      workers_.emplace_back([this] {
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex_);
            this->condition_.wait(lock, [this] { return this->stop_ || !this->tasks_.empty(); });
            if (this->stop_ && this->tasks_.empty()) {
              return;
            }
            task = std::move(this->tasks_.front());
            this->tasks_.pop();
          }
          task();
        }
      });
    }
  }

  template <typename F, typename... Args, typename return_t = std::invoke_result_t<F, Args...>>
  auto Submit(F &&f, Args &&...args) -> std::future<return_t> {
    auto func = [f = std::forward<F>(f), args = std::forward_as_tuple(args...)]() mutable {
      return std::apply(f, std::move(args));
    };
    auto task = std::make_shared<std::packaged_task<return_t()>>(std::move(func));
    {
      std::unique_lock lock(queue_mutex_);
      tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return task->get_future();
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      stop_ = true;
    }
    condition_.notify_all();
    for (std::thread &worker : workers_) {
      worker.join();
    }
  }

 private:
  // need to keep track of threads so we can join them
  std::vector<std::thread> workers_;
  // the task queue
  std::queue<std::function<void()>> tasks_;

  // synchronization
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_{};
};

/**
 * @brief Represents a Write or Read request for the DiskManager to execute.
 */
struct DiskRequest {
  DiskRequest(bool is_write, char *data, page_id_t page_id) : is_write_(is_write), data_(data), page_id_(page_id) {}
  DiskRequest(bool is_write, char *data, page_id_t page_id, std::promise<bool> &&callback)
      : is_write_(is_write), data_(data), page_id_(page_id), callback_(std::move(callback)) {}

  /** Flag indicating whether the request is a write or a read. */
  bool is_write_;

  /**
   *  Pointer to the start of the memory location where a page is either:
   *   1. being read into from disk (on a read).
   *   2. being written out to disk (on a write).
   */
  char *data_;

  /** ID of the page being read from / written to disk. */
  page_id_t page_id_;

  /** Callback used to signal to the request issuer when the request has been completed. */
  std::promise<bool> callback_;
};

/**
 * @brief The DiskScheduler schedules disk read and write operations.
 *
 * A request is scheduled by calling DiskScheduler::Schedule() with an appropriate DiskRequest object. The scheduler
 * maintains a background worker thread that processes the scheduled requests using the disk manager. The background
 * thread is created in the DiskScheduler constructor and joined in its destructor.
 */
class DiskScheduler {
 public:
  explicit DiskScheduler(DiskManager *disk_manager);

  /**
   * TODO(P1): Add implementation
   *
   * @brief Schedules a request for the DiskManager to execute.
   *
   * @param r The request to be scheduled.
   */
  auto Schedule(DiskRequest r) -> std::future<void>;

  using DiskSchedulerPromise = std::promise<bool>;

  /**
   * @brief Create a Promise object. If you want to implement your own version of promise, you can change this function
   * so that our test cases can use your promise implementation.
   *
   * @return std::promise<bool>
   */
  auto CreatePromise() -> DiskSchedulerPromise { return {}; };

 private:
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_;

  ThreadPool threadpool_{16};
};
}  // namespace bustub
