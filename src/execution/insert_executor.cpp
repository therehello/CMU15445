//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"

#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      table_info_(exec_ctx->GetCatalog()->GetTable(plan->GetTableOid())),
      index_infos_(exec_ctx->GetCatalog()->GetTableIndexes(table_info_->name_)),
      child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() { child_executor_->Init(); }

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (is_end_) {
    return false;
  }

  int inserted_count = 0;
  for (; child_executor_->Next(tuple, rid); inserted_count++) {
    *rid = table_info_->table_->InsertTuple(TupleMeta{}, *tuple).value();
    UpdateIndexes(*tuple, *rid);
  }

  Value res(TypeId::INTEGER, inserted_count);
  *tuple = Tuple(std::vector{res}, &GetOutputSchema());

  return is_end_ = true;
}

void InsertExecutor::UpdateIndexes(Tuple &tuple, RID &rid) const {
  for (auto &index_info : index_infos_) {
    std::vector<Value> index;
    for (const auto &col : index_info->key_schema_.GetColumns()) {
      auto idx = table_info_->schema_.GetColIdx(col.GetName());
      auto value = tuple.GetValue(&table_info_->schema_, idx);
      index.push_back(value);
    }
    index_info->index_->InsertEntry(Tuple(index, &table_info_->schema_), rid, exec_ctx_->GetTransaction());
  }
}

}  // namespace bustub
