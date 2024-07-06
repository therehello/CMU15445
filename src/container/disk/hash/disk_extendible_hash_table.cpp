//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_extendible_hash_table.cpp
//
// Identification: src/container/disk/hash/disk_extendible_hash_table.cpp
//
// Copyright (c) 2015-2023, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "common/config.h"
#include "container/disk/hash/disk_extendible_hash_table.h"
#include "storage/index/int_comparator.h"
#include "storage/page/extendible_htable_bucket_page.h"
#include "storage/page/extendible_htable_directory_page.h"
#include "storage/page/extendible_htable_header_page.h"
#include "storage/page/page_guard.h"

namespace bustub {

template <typename K, typename V, typename KC>
DiskExtendibleHashTable<K, V, KC>::DiskExtendibleHashTable(std::string name, BufferPoolManager *bpm, const KC &cmp,
                                                           const HashFunction<K> &hash_fn, uint32_t header_max_depth,
                                                           uint32_t directory_max_depth, uint32_t bucket_max_size)
    : index_name_(std::move(name)),
      bpm_(bpm),
      cmp_(cmp),
      hash_fn_(std::move(hash_fn)),
      header_max_depth_(header_max_depth),
      directory_max_depth_(directory_max_depth),
      bucket_max_size_(bucket_max_size) {
  auto header_guard = bpm_->NewPageGuarded(&header_page_id_);
  auto header_page = header_guard.AsMut<ExtendibleHTableHeaderPage>();
  header_page->Init(header_max_depth);
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::GetValue(const K &key, std::vector<V> *result, Transaction *transaction) const
    -> bool {
  auto hash = Hash(key);

  auto header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header_page = header_guard.As<ExtendibleHTableHeaderPage>();
  page_id_t directory_page_id = header_page->GetDirectoryPageId(header_page->HashToDirectoryIndex(hash));
  if (directory_page_id == INVALID_PAGE_ID) {
    return false;
  }
  header_guard.Drop();

  auto directory_guard = bpm_->FetchPageRead(directory_page_id);
  auto directory_page = directory_guard.As<ExtendibleHTableDirectoryPage>();
  auto bucket_page_id = directory_page->GetBucketPageId(directory_page->HashToBucketIndex(hash));
  if (bucket_page_id == INVALID_PAGE_ID) {
    return false;
  }

  auto bucket_guard = bpm_->FetchPageRead(bucket_page_id);
  directory_guard.Drop();
  auto bucket_page = bucket_guard.template As<ExtendibleHTableBucketPage<K, V, KC>>();
  V value;
  if (!bucket_page->Lookup(key, value, cmp_)) {
    return false;
  }
  bucket_guard.Drop();

  result->push_back(value);
  return true;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/

template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::Insert(const K &key, const V &value, Transaction *transaction) -> bool {
  auto hash = Hash(key);

  auto header_guard = bpm_->FetchPageWrite(header_page_id_);
  auto header_page = header_guard.AsMut<ExtendibleHTableHeaderPage>();
  auto directory_idx = header_page->HashToDirectoryIndex(hash);
  page_id_t directory_page_id = header_page->GetDirectoryPageId(directory_idx);
  if (directory_page_id == INVALID_PAGE_ID) {
    return InsertToNewDirectory(header_page, directory_idx, hash, key, value);
  }
  header_guard.Drop();

  auto directory_guard = bpm_->FetchPageWrite(directory_page_id);
  auto directory_page = directory_guard.AsMut<ExtendibleHTableDirectoryPage>();
  auto bucket_idx = directory_page->HashToBucketIndex(hash);
  auto bucket_page_id = directory_page->GetBucketPageId(bucket_idx);
  if (bucket_page_id == INVALID_PAGE_ID) {
    return InsertToNewBucket(directory_page, bucket_idx, key, value);
  }

  auto bucket_guard = bpm_->FetchPageWrite(bucket_page_id);
  auto bucket_page = bucket_guard.template AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
  if (V tmp; bucket_page->Lookup(key, tmp, cmp_)) {
    return false;
  }
  if (!bucket_page->IsFull()) {
    return bucket_page->Insert(key, value, cmp_);
  }

  while (bucket_page->IsFull()) {
    if (directory_page->GetGlobalDepth() == directory_page->GetLocalDepth(bucket_idx)) {
      if (directory_page->GetGlobalDepth() == directory_page->GetMaxDepth()) {
        return false;
      }
      directory_page->IncrGlobalDepth();
    }

    page_id_t new_bucket_page_id;
    auto new_bucket_guard = bpm_->NewPageGuarded(&new_bucket_page_id).UpgradeWrite();
    auto new_bucket_page = new_bucket_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
    if (new_bucket_page == nullptr) {
      return false;
    }
    new_bucket_page->Init(bucket_max_size_);

    directory_page->IncrLocalDepth(bucket_idx);
    auto new_local_depth = directory_page->GetLocalDepth(bucket_idx);
    auto other_bucket_idx = directory_page->GetSplitImageIndex(bucket_idx);

    InsertUpdateDirectoryMapping(directory_page, other_bucket_idx, new_bucket_page_id, new_local_depth);

    std::vector<uint32_t> remove_idx;
    for (uint32_t i = 0; i < bucket_page->Size(); i++) {
      auto &[k, v] = bucket_page->EntryAt(i);
      auto hash_k = Hash(k);
      auto rehash_bucket_pege_id = directory_page->GetBucketPageId(directory_page->HashToBucketIndex(hash_k));
      if (rehash_bucket_pege_id == new_bucket_page_id) {
        new_bucket_page->Insert(k, v, cmp_);
        remove_idx.push_back(i);
      }
    }
    bucket_page->Remove(remove_idx);

    auto rehash_bucket_pege_id = directory_page->GetBucketPageId(directory_page->HashToBucketIndex(hash));
    if (rehash_bucket_pege_id == new_bucket_page_id) {
      if (new_bucket_page->Insert(key, value, cmp_)) {
        return true;
      }
      assert(new_bucket_page->IsFull());
      bucket_idx = other_bucket_idx;
      bucket_guard = std::move(new_bucket_guard);
      bucket_page = new_bucket_page;
    } else {
      if (bucket_page->Insert(key, value, cmp_)) {
        return true;
      }
      assert(bucket_page->IsFull());
    }
  }
  return true;
}

template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::InsertToNewDirectory(ExtendibleHTableHeaderPage *header_page,
                                                             uint32_t directory_idx, uint32_t hash, const K &key,
                                                             const V &value) -> bool {
  page_id_t directory_page_id;
  auto directory_guard = bpm_->NewPageGuarded(&directory_page_id).UpgradeWrite();
  auto directory_page = directory_guard.AsMut<ExtendibleHTableDirectoryPage>();
  directory_page->Init(directory_max_depth_);
  header_page->SetDirectoryPageId(directory_idx, directory_page_id);
  auto bucket_idx = directory_page->HashToBucketIndex(hash);
  return InsertToNewBucket(directory_page, bucket_idx, key, value);
}

template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::InsertToNewBucket(ExtendibleHTableDirectoryPage *directory_page,
                                                          uint32_t bucket_idx, const K &key, const V &value) -> bool {
  page_id_t bucket_page_id;
  auto bucket_guard = bpm_->NewPageGuarded(&bucket_page_id).UpgradeWrite();
  auto bucket_page = bucket_guard.AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
  bucket_page->Init(bucket_max_size_);
  directory_page->SetBucketPageId(bucket_idx, bucket_page_id);
  directory_page->SetLocalDepth(bucket_idx, 0);
  return bucket_page->Insert(key, value, cmp_);
}

template <typename K, typename V, typename KC>
void DiskExtendibleHashTable<K, V, KC>::InsertUpdateDirectoryMapping(ExtendibleHTableDirectoryPage *directory_page,
                                                                     uint32_t other_bucket_idx,
                                                                     page_id_t new_bucket_page_id,
                                                                     uint32_t new_local_depth) {
  auto distance = 1 << new_local_depth;
  /** 先提前设置好一下，否则 directory_page->GetSplitImageIndex(other_bucket_idx) 获取错误 */
  directory_page->SetLocalDepth(other_bucket_idx, new_local_depth);
  auto bucket_idx = directory_page->GetSplitImageIndex(other_bucket_idx);
  while (bucket_idx < directory_page->Size() && other_bucket_idx < directory_page->Size()) {
    directory_page->SetLocalDepth(bucket_idx, new_local_depth);
    directory_page->SetBucketPageId(other_bucket_idx, new_bucket_page_id);
    directory_page->SetLocalDepth(other_bucket_idx, new_local_depth);
    bucket_idx += distance;
    other_bucket_idx += distance;
  }
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
template <typename K, typename V, typename KC>
auto DiskExtendibleHashTable<K, V, KC>::Remove(const K &key, Transaction *transaction) -> bool {
  auto hash = Hash(key);

  auto header_guard = bpm_->FetchPageRead(header_page_id_);
  auto header_page = header_guard.As<ExtendibleHTableHeaderPage>();
  page_id_t directory_page_id = header_page->GetDirectoryPageId(header_page->HashToDirectoryIndex(hash));
  if (directory_page_id == INVALID_PAGE_ID) {
    return false;
  }
  header_guard.Drop();

  auto directory_guard = bpm_->FetchPageWrite(directory_page_id);
  auto directory_page = directory_guard.AsMut<ExtendibleHTableDirectoryPage>();
  auto bucket_idx = directory_page->HashToBucketIndex(hash);
  auto bucket_page_id = directory_page->GetBucketPageId(bucket_idx);
  if (bucket_page_id == INVALID_PAGE_ID) {
    return false;
  }

  auto bucket_guard = bpm_->FetchPageWrite(bucket_page_id);
  auto bucket_page = bucket_guard.template AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
  if (!bucket_page->Remove(key, cmp_)) {
    return false;
  }

  while (bucket_page->IsEmpty()) {
    bucket_guard.Drop();
    auto local_depth = directory_page->GetLocalDepth(bucket_idx);
    if (local_depth == 0) {
      break;
    }

    auto aother_bucket_idx = directory_page->GetSplitImageIndex(bucket_idx);
    auto aother_bucket_page_id = directory_page->GetBucketPageId(aother_bucket_idx);
    if (aother_bucket_page_id == INVALID_PAGE_ID) {
      break;
    }
    auto aother_local_depth = directory_page->GetLocalDepth(aother_bucket_idx);
    if (local_depth != aother_local_depth) {
      break;
    }

    RemoveUpdateDirectoryMapping(directory_page, aother_bucket_idx, aother_bucket_page_id, local_depth);
    if (local_depth == 1) {
      break;
    }

    aother_bucket_idx = directory_page->GetSplitImageIndex(aother_bucket_idx);
    aother_bucket_page_id = directory_page->GetBucketPageId(aother_bucket_idx);
    if (aother_bucket_page_id == INVALID_PAGE_ID) {
      break;
    }

    auto aother_bucket_guard = bpm_->FetchPageWrite(aother_bucket_page_id);
    auto aother_bucket_page = aother_bucket_guard.template AsMut<ExtendibleHTableBucketPage<K, V, KC>>();
    bucket_idx = aother_bucket_idx;
    bucket_page_id = aother_bucket_page_id;
    bucket_guard = std::move(aother_bucket_guard);
    bucket_page = aother_bucket_page;
  }

  directory_page->Shrink();

  return true;
}

template <typename K, typename V, typename KC>
void DiskExtendibleHashTable<K, V, KC>::RemoveUpdateDirectoryMapping(ExtendibleHTableDirectoryPage *directory_page,
                                                                     uint32_t other_bucket_idx,
                                                                     page_id_t other_bucket_page_id,
                                                                     uint32_t local_depth) {
  auto distance = 1 << local_depth;
  auto bucket_idx = directory_page->GetSplitImageIndex(other_bucket_idx);
  while (bucket_idx < directory_page->Size() && other_bucket_idx < directory_page->Size()) {
    directory_page->DecrLocalDepth(bucket_idx);
    directory_page->DecrLocalDepth(other_bucket_idx);
    directory_page->SetBucketPageId(bucket_idx, other_bucket_page_id);
    bucket_idx += distance;
    other_bucket_idx += distance;
  }
}

template class DiskExtendibleHashTable<int, int, IntComparator>;
template class DiskExtendibleHashTable<GenericKey<4>, RID, GenericComparator<4>>;
template class DiskExtendibleHashTable<GenericKey<8>, RID, GenericComparator<8>>;
template class DiskExtendibleHashTable<GenericKey<16>, RID, GenericComparator<16>>;
template class DiskExtendibleHashTable<GenericKey<32>, RID, GenericComparator<32>>;
template class DiskExtendibleHashTable<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
