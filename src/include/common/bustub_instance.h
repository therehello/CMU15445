//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// bustub_instance.h
//
// Identification: src/include/common/bustub_instance.h
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager_instance.h"
#include "common/config.h"
#include "concurrency/lock_manager.h"
#include "execution/executor_context.h"
#include "recovery/checkpoint_manager.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_manager.h"

namespace bustub {

class BustubInstance {
 private:
  /**
   * Get the executor context from the BusTub instance.
   */
  auto MakeExecutorContext(Transaction *txn) -> std::unique_ptr<ExecutorContext>;

 public:
  explicit BustubInstance(const std::string &db_file_name);

  ~BustubInstance();

  /**
   * Execute a SQL query in the BusTub instance.
   */
  auto ExecuteSql(const std::string &sql) -> std::vector<std::string>;

  /**
   * FOR TEST ONLY. Generate test tables in this BusTub instance.
   * It's used in the shell to predefine some tables, as we don't support
   * create / drop table and insert for now. Should remove it in the future.
   */
  void GenerateTestTable();

  // TODO(chi): change to unique_ptr. Currently they're directly referenced by recovery test, so
  // we cannot do anything on them until someone decides to refactor the recovery test.

  DiskManager *disk_manager_;
  BufferPoolManager *buffer_pool_manager_;
  LockManager *lock_manager_;
  TransactionManager *transaction_manager_;
  LogManager *log_manager_;
  CheckpointManager *checkpoint_manager_;
  Catalog *catalog_;
};

}  // namespace bustub
