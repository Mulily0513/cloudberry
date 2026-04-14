/*-------------------------------------------------------------------------
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * pax_vec_reader.cc
 *
 * IDENTIFICATION
 *	  contrib/pax_storage/src/cpp/storage/vec/pax_vec_reader.cc
 *
 *-------------------------------------------------------------------------
 */

#include "storage/vec/pax_vec_reader.h"

#include "comm/guc.h"
#include "comm/pax_memory.h"
#include "storage/micro_partition_stats.h"
#include "storage/oper/pax_stats.h"
#include "storage/pax_itemptr.h"
#include "storage/vec/pax_vec_adapter.h"
#include "storage/filter/pax_sparse_filter.h"

#include <arrow/scalar.h>
#include <arrow/compute/exec/topk_threshold_state.h>

#ifdef VEC_BUILD

namespace pax {

PaxVecReader::PaxVecReader(std::unique_ptr<MicroPartitionReader> &&reader,
                           std::shared_ptr<VecAdapter> adapter,
                           std::shared_ptr<PaxFilter> filter)
    : adapter_(std::move(adapter)),
      current_group_index_(0),
      ctid_offset_(0),
      filter_(std::move(filter)) {
  Assert(reader && adapter_);
  SetReader(std::move(reader));
}

void PaxVecReader::Open(const ReaderOptions &options) {
  auto visimap = options.visibility_bitmap;
  reader_->Open(options);
  if (visimap) {
    adapter_->SetVisibitilyMapInfo(visimap);
  }
}

void PaxVecReader::Close() { reader_->Close(); }

PaxVecReader::~PaxVecReader() {}

std::shared_ptr<arrow::RecordBatch> PaxVecReader::ReadBatch(
    PaxFragmentInterface *frag) {
  auto desc = adapter_->GetRelationTupleDesc();
  std::shared_ptr<arrow::RecordBatch> result;
  size_t flush_nums_of_rows = 0;

retry_next_group:
  if (!working_group_) {
    if (current_group_index_ >= reader_->GetGroupNums()) {
      return nullptr;
    }
    auto group_index = current_group_index_++;
    auto info = reader_->GetGroupStatsInfo(group_index);
    if (filter_ && !filter_->ExecSparseFilter(
                       *info, desc, PaxSparseFilter::StatisticsKind::kGroup)) {
      goto retry_next_group;
    }

    // TopK Runtime Filter: skip group if all rows are worse than threshold
    if (topk_threshold_ && topk_threshold_->IsSet()) {
      if (EvalTopKThresholdSkip(*info, desc)) {
        goto retry_next_group;
      }
    }

    working_group_ = reader_->ReadGroup(group_index);
    adapter_->SetDataSource(working_group_->GetAllColumns().get(),
                            working_group_->GetRowOffset());
  }

  if (!adapter_->AppendToVecBuffer()) {
    working_group_ = nullptr;
    goto retry_next_group;
  }

  result = adapter_->FlushVecBuffer(ctid_offset_, frag, flush_nums_of_rows);
  ctid_offset_ += flush_nums_of_rows;
  if (!result) {
    working_group_ = nullptr;
    goto retry_next_group;
  }

  Assert(flush_nums_of_rows > 0);
  return result;
}

bool PaxVecReader::ReadTuple(TupleTableSlot *slot) {
  auto desc = adapter_->GetRelationTupleDesc();
retry_read_group:
  if (!working_group_) {
    if (current_group_index_ >= reader_->GetGroupNums()) {
      return false;
    }
    auto group_index = current_group_index_++;
    auto info = reader_->GetGroupStatsInfo(group_index);
    if (filter_ && !filter_->ExecSparseFilter(
                       *info, desc, PaxSparseFilter::StatisticsKind::kGroup)) {
      goto retry_read_group;
    }

    working_group_ = reader_->ReadGroup(group_index);

    adapter_->SetDataSource(working_group_->GetAllColumns().get(),
                            working_group_->GetRowOffset());
  }

  auto flush_nums_of_rows = adapter_->AppendToVecBuffer();
  if (flush_nums_of_rows == -1) {
    working_group_ = nullptr;
    goto retry_read_group;
  }

  if (flush_nums_of_rows == 0) {
    goto retry_read_group;
  }

  adapter_->FlushVecBuffer(slot);

  return true;
}

int PaxVecReader::GetTuple(TupleTableSlot *slot, size_t row_index) {
  CBDB_RAISE(cbdb::CException::ExType::kExTypeLogicError);
}

size_t PaxVecReader::GetGroupNums() {
  CBDB_RAISE(cbdb::CException::ExType::kExTypeLogicError);
}

size_t PaxVecReader::GetTupleCountsInGroup(size_t group_index) {
  CBDB_RAISE(cbdb::CException::ExType::kExTypeLogicError);
}

std::unique_ptr<ColumnStatsProvider> PaxVecReader::GetGroupStatsInfo(
    size_t group_index) {
  CBDB_RAISE(cbdb::CException::ExType::kExTypeLogicError);
}

std::unique_ptr<MicroPartitionReader::Group> PaxVecReader::ReadGroup(
    size_t index) {
  CBDB_RAISE(cbdb::CException::ExType::kExTypeLogicError);
}

// ---------------------------------------------------------------------------
// TopK Runtime Filter: EvalTopKThresholdSkip
// ---------------------------------------------------------------------------

// Convert Arrow physical-type Scalar to PG Datum for comparison with
// PAX group min/max statistics. Handles the common ClickBench types.
static std::pair<Datum, bool> ThresholdScalarToDatum(
    const std::shared_ptr<arrow::Scalar> &scalar, Form_pg_attribute attr) {
  if (!scalar || !scalar->is_valid) return {0, false};

  switch (scalar->type->id()) {
    case arrow::Type::BOOL: {
      auto v = static_cast<const arrow::BooleanScalar*>(scalar.get())->value;
      return {BoolGetDatum(v), true};
    }
    case arrow::Type::INT8: {
      auto v = static_cast<const arrow::Int8Scalar*>(scalar.get())->value;
      return {Int8GetDatum(v), true};
    }
    case arrow::Type::INT16: {
      auto v = static_cast<const arrow::Int16Scalar*>(scalar.get())->value;
      return {Int16GetDatum(v), true};
    }
    case arrow::Type::INT32: {
      auto v = static_cast<const arrow::Int32Scalar*>(scalar.get())->value;
      return {Int32GetDatum(v), true};
    }
    case arrow::Type::INT64: {
      auto v = static_cast<const arrow::Int64Scalar*>(scalar.get())->value;
      return {Int64GetDatum(v), true};
    }
    case arrow::Type::FLOAT: {
      auto v = static_cast<const arrow::FloatScalar*>(scalar.get())->value;
      return {Float4GetDatum(v), true};
    }
    case arrow::Type::DOUBLE: {
      auto v = static_cast<const arrow::DoubleScalar*>(scalar.get())->value;
      return {Float8GetDatum(v), true};
    }
    case arrow::Type::BINARY: {
      // Physical type for STRING columns (StringType::PhysicalType = BinaryType)
      auto *bs = static_cast<const arrow::BinaryScalar*>(scalar.get());
      const char *s = reinterpret_cast<const char *>(bs->value->data());
      auto len = static_cast<size_t>(bs->value->size());
      switch (attr->atttypid) {
        case TEXTOID:
          return {PointerGetDatum(cbdb::CstringToText(s, len)), true};
        case VARCHAROID:
          return {PointerGetDatum(cbdb::VarcharInput(s, len, attr->atttypmod)), true};
        case BPCHAROID:
          return {PointerGetDatum(cbdb::BpcharInput(s, len, attr->atttypmod)), true};
        default:
          break;
      }
      return {0, false};
    }
    case arrow::Type::STRING: {
      auto *ss = static_cast<const arrow::StringScalar*>(scalar.get());
      const char *s = reinterpret_cast<const char *>(ss->value->data());
      auto len = static_cast<size_t>(ss->value->size());
      switch (attr->atttypid) {
        case TEXTOID:
          return {PointerGetDatum(cbdb::CstringToText(s, len)), true};
        case VARCHAROID:
          return {PointerGetDatum(cbdb::VarcharInput(s, len, attr->atttypmod)), true};
        case BPCHAROID:
          return {PointerGetDatum(cbdb::BpcharInput(s, len, attr->atttypmod)), true};
        default:
          break;
      }
      return {0, false};
    }
    default:
      break;
  }
  return {0, false};
}

bool PaxVecReader::EvalTopKThresholdSkip(
    const ColumnStatsProvider& stats, TupleDesc desc) {
  // 1. Get current threshold
  auto threshold_scalar = topk_threshold_->Get();
  if (!threshold_scalar || !threshold_scalar->is_valid) return false;

  // 2. Get sort column info from TopKThresholdState
  int col = topk_threshold_->sort_column_index();
  auto order = topk_threshold_->sort_order();
  Oid collation = static_cast<Oid>(topk_threshold_->collation());

  // 3. Check sort column statistics availability
  if (col < 0 || col >= stats.ColumnSize()) return false;
  const auto& data_stats = stats.DataStats(col);
  if (!data_stats.has_minimal() || !data_stats.has_maximum()) return false;

  // 4. Arrow Scalar → Datum
  Form_pg_attribute attr = TupleDescAttr(desc, col);
  auto [threshold_datum, ok] = ThresholdScalarToDatum(threshold_scalar, attr);
  if (!ok) return false;

  // 5. Group min/max → Datum
  Datum group_min = pax::MicroPartitionStats::FromValue(
      data_stats.minimal(), attr->attlen, attr->attbyval, col);
  Datum group_max = pax::MicroPartitionStats::FromValue(
      data_stats.maximum(), attr->attlen, attr->attbyval, col);

  // 6. Compare using PAX's OperMinMaxFunc infrastructure
  OperMinMaxFunc cmp_func;
  bool skip = false;
  if (order == arrow::compute::SortOrder::Ascending) {
    // ASC: threshold is ceiling. If group_min > threshold → skip
    if (pax::MinMaxGetStrategyProcinfo(attr->atttypid, attr->atttypid,
                                       collation, cmp_func,
                                       BTGreaterStrategyNumber))
      skip = cmp_func(&group_min, &threshold_datum, collation);
  } else {
    // DESC: threshold is floor. If group_max < threshold → skip
    if (pax::MinMaxGetStrategyProcinfo(attr->atttypid, attr->atttypid,
                                       collation, cmp_func,
                                       BTLessStrategyNumber))
      skip = cmp_func(&group_max, &threshold_datum, collation);
  }

  // ThresholdScalarToDatum palloc's a fresh varlena for non-byval types
  // (TEXT/VARCHAR/BPCHAR); free it here. group_min/group_max are pointers
  // into the protobuf stats message and must NOT be pfreed.
  if (!attr->attbyval && DatumGetPointer(threshold_datum) != nullptr)
    pfree(DatumGetPointer(threshold_datum));

  return skip;
}

}  // namespace pax

#endif  // VEC_BUILD
