// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
// This file is copied from
// https://github.com/ClickHouse/ClickHouse/blob/master/src/Functions/IsNull.cpp
// and modified by Doris

#include <stddef.h>

#include <algorithm>
#include <boost/iterator/iterator_facade.hpp>
#include <memory>
#include <string>

#include "common/status.h"
#include "vec/aggregate_functions/aggregate_function.h"
#include "vec/columns/column.h"
#include "vec/columns/column_nullable.h"
#include "vec/core/block.h"
#include "vec/core/column_numbers.h"
#include "vec/core/column_with_type_and_name.h"
#include "vec/core/field.h"
#include "vec/data_types/data_type_number.h"
#include "vec/functions/function.h"

namespace doris {
class FunctionContext;
} // namespace doris

namespace doris::vectorized {

/// Implements the function is_null which returns true if a value
/// is null, false otherwise.
class FunctionIsNull : public IFunction {
public:
    static constexpr auto name = "is_null_pred";

    static FunctionPtr create() { return std::make_shared<FunctionIsNull>(); }

    std::string get_name() const override { return name; }

    size_t get_number_of_arguments() const override { return 1; }
    bool use_default_implementation_for_nulls() const override { return false; }

    DataTypePtr get_return_type_impl(const DataTypes&) const override {
        return std::make_shared<DataTypeUInt8>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        const ColumnWithTypeAndName& elem = block.get_by_position(arguments[0]);
        if (auto* nullable = check_and_get_column<ColumnNullable>(*elem.column)) {
            /// Merely return the embedded null map.
            block.get_by_position(result).column = nullable->get_null_map_column_ptr();
        } else {
            /// Since no element is nullable, return a zero-constant column representing
            /// a zero-filled null map.
            block.get_by_position(result).column = DataTypeUInt8().create_column_const(
                    elem.column->size(), Field::create_field<TYPE_BOOLEAN>(0));
        }
        return Status::OK();
    }

    Status evaluate_inverted_index(
            const ColumnsWithTypeAndName& arguments,
            const std::vector<vectorized::IndexFieldNameAndTypePair>& data_type_with_names,
            std::vector<segment_v2::IndexIterator*> iterators, uint32_t num_rows,
            segment_v2::InvertedIndexResultBitmap& bitmap_result) const override {
        if (iterators.empty() || iterators[0] == nullptr) {
            return Status::OK();
        }
        auto* index_iter = iterators[0];
        if (index_iter->has_null()) {
            segment_v2::InvertedIndexQueryCacheHandle null_bitmap_cache_handle;
            RETURN_IF_ERROR(index_iter->read_null_bitmap(&null_bitmap_cache_handle));
            std::shared_ptr<roaring::Roaring> null_bitmap = null_bitmap_cache_handle.get_bitmap();
            // only inverted index has null bitmap, so we can calculate
            if (null_bitmap) {
                // null_bitmap is null bitmap
                bitmap_result = segment_v2::InvertedIndexResultBitmap(null_bitmap, null_bitmap);
            }
        }
        return Status::OK();
    }
};

} // namespace doris::vectorized