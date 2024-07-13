//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"
#include <memory>
#include "storage/table/table_iterator.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan), table_info(exec_ctx->GetCatalog()->GetTable(plan_->GetTableOid())) {}

void SeqScanExecutor::Init() { iter_ = std::make_unique<TableIterator>(table_info->table_->MakeIterator()); }

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  for (; !iter_->IsEnd(); ++(*iter_)) {
    auto [tuple_meta, _tuple] = iter_->GetTuple();
    if (tuple_meta.is_deleted_) {
      continue;
    }
    *tuple = _tuple;
    *rid = iter_->GetRID();
    if (plan_->filter_predicate_) {
      if (!plan_->filter_predicate_->Evaluate(tuple, table_info->schema_).GetAs<bool>()) {
        continue;
      }
    }
    ++(*iter_);
    return true;
  }
  return false;
}

}  // namespace bustub
