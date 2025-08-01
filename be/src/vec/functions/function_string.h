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

#pragma once

#include <glog/logging.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/locale.hpp>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "common/compiler_util.h" // IWYU pragma: keep
#include "common/exception.h"
#include "common/status.h"
#include "runtime/decimalv2_value.h"
#include "runtime/string_search.hpp"
#include "util/sha.h"
#include "util/string_util.h"
#include "util/utf8_check.h"
#include "vec/aggregate_functions/aggregate_function.h"
#include "vec/columns/column.h"
#include "vec/columns/column_const.h"
#include "vec/columns/column_vector.h"
#include "vec/common/hash_table/phmap_fwd_decl.h"
#include "vec/common/int_exp.h"
#include "vec/common/memcmp_small.h"
#include "vec/common/memcpy_small.h"
#include "vec/common/pod_array.h"
#include "vec/common/pod_array_fwd.h"
#include "vec/core/block.h"
#include "vec/core/column_numbers.h"
#include "vec/core/column_with_type_and_name.h"
#include "vec/core/types.h"
#include "vec/data_types/data_type.h"
#include "vec/utils/template_helpers.hpp"

#ifndef USE_LIBCPP
#include <memory_resource>
#define PMR std::pmr
#else
#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/vector.hpp>
#define PMR boost::container::pmr
#endif

#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "exprs/math_functions.h"
#include "pugixml.hpp"
#include "udf/udf.h"
#include "util/md5.h"
#include "util/simd/vstring_function.h"
#include "util/sm3.h"
#include "util/url_coding.h"
#include "util/url_parser.h"
#include "vec/columns/column_array.h"
#include "vec/columns/column_decimal.h"
#include "vec/columns/column_nullable.h"
#include "vec/columns/column_string.h"
#include "vec/common/assert_cast.h"
#include "vec/common/pinyin.h"
#include "vec/common/string_ref.h"
#include "vec/data_types/data_type_array.h"
#include "vec/data_types/data_type_decimal.h"
#include "vec/data_types/data_type_nullable.h"
#include "vec/data_types/data_type_number.h"
#include "vec/data_types/data_type_string.h"
#include "vec/functions/function.h"
#include "vec/functions/function_helpers.h"
#include "vec/utils/stringop_substring.h"
#include "vec/utils/util.hpp"

namespace doris::vectorized {

class FunctionStrcmp : public IFunction {
public:
    static constexpr auto name = "strcmp";

    static FunctionPtr create() { return std::make_shared<FunctionStrcmp>(); }

    String get_name() const override { return name; }

    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeInt8>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        const auto& [arg0_column, arg0_const] =
                unpack_if_const(block.get_by_position(arguments[0]).column);
        const auto& [arg1_column, arg1_const] =
                unpack_if_const(block.get_by_position(arguments[1]).column);

        auto result_column = ColumnInt8::create(input_rows_count);

        if (auto arg0 = check_and_get_column<ColumnString>(arg0_column.get())) {
            if (auto arg1 = check_and_get_column<ColumnString>(arg1_column.get())) {
                if (arg0_const) {
                    scalar_vector(arg0->get_data_at(0), *arg1, *result_column);
                } else if (arg1_const) {
                    vector_scalar(*arg0, arg1->get_data_at(0), *result_column);
                } else {
                    vector_vector(*arg0, *arg1, *result_column);
                }
            }
        }

        block.replace_by_position(result, std::move(result_column));
        return Status::OK();
    }

private:
    static void scalar_vector(const StringRef str, const ColumnString& vec1, ColumnInt8& res) {
        size_t size = vec1.size();
        for (size_t i = 0; i < size; ++i) {
            res.get_data()[i] = str.compare(vec1.get_data_at(i));
        }
    }

    static void vector_scalar(const ColumnString& vec0, const StringRef str, ColumnInt8& res) {
        size_t size = vec0.size();
        for (size_t i = 0; i < size; ++i) {
            res.get_data()[i] = vec0.get_data_at(i).compare(str);
        }
    }

    static void vector_vector(const ColumnString& vec0, const ColumnString& vec1, ColumnInt8& res) {
        size_t size = vec0.size();
        for (size_t i = 0; i < size; ++i) {
            res.get_data()[i] = vec0.get_data_at(i).compare(vec1.get_data_at(i));
        }
    }
};

class FunctionAutoPartitionName : public IFunction {
public:
    static constexpr auto name = "auto_partition_name";
    static FunctionPtr create() { return std::make_shared<FunctionAutoPartitionName>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }
    bool use_default_implementation_for_nulls() const override { return false; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        size_t argument_size = arguments.size();
        auto const_null_map = ColumnUInt8::create(input_rows_count, 0);
        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        std::vector<const ColumnString::Chars*> chars_list(argument_size);
        std::vector<const ColumnString::Offsets*> offsets_list(argument_size);
        std::vector<bool> is_const_args(argument_size);
        std::vector<const ColumnUInt8::Container*> null_list(argument_size);
        std::vector<ColumnPtr> argument_null_columns(argument_size);

        std::vector<ColumnPtr> argument_columns(argument_size);
        for (int i = 0; i < argument_size; ++i) {
            argument_columns[i] =
                    block.get_by_position(arguments[i]).column->convert_to_full_column_if_const();
            if (const auto* nullable =
                        check_and_get_column<const ColumnNullable>(*argument_columns[i])) {
                null_list[i] = &nullable->get_null_map_data();
                argument_null_columns[i] = nullable->get_null_map_column_ptr();
                argument_columns[i] = nullable->get_nested_column_ptr();
            } else {
                null_list[i] = &const_null_map->get_data();
            }

            const auto& [col, is_const] =
                    unpack_if_const(block.get_by_position(arguments[i]).column);

            const auto* col_str = assert_cast<const ColumnString*>(argument_columns[i].get());
            chars_list[i] = &col_str->get_chars();
            offsets_list[i] = &col_str->get_offsets();
            is_const_args[i] = is_const;
        }

        auto res = ColumnString::create();
        auto& res_data = res->get_chars();
        auto& res_offset = res->get_offsets();
        res_offset.resize(input_rows_count);

        const char* partition_type = chars_list[0]->raw_data();
        // partition type is list|range
        if (std::strncmp(partition_type, "list", 4) == 0) {
            return _auto_partition_type_of_list(chars_list, offsets_list, is_const_args, null_list,
                                                res_data, res_offset, input_rows_count,
                                                argument_size, block, result, res);
        } else {
            return _auto_partition_type_of_range(chars_list, offsets_list, is_const_args, res_data,
                                                 res_offset, input_rows_count, argument_size, block,
                                                 result, res);
        }
        return Status::OK();
    }

private:
    std::u16string _string_to_u16string(const std::string& str) const {
        return boost::locale::conv::utf_to_utf<char16_t>(str);
    }

    std::string _string_to_unicode(const std::u16string& s) const {
        std::string res_s;
        res_s.reserve(s.size());
        if (s.length() > 0 && s[0] == '-') {
            res_s += '_';
        }
        for (int i = 0; i < s.length(); i++) {
            char ch = s[i];
            if (std::isalnum(ch)) {
                res_s += ch;
            } else {
                int unicodeValue = _get_code_point_at(s, i);
                res_s += fmt::format("{:02x}", static_cast<uint32_t>(unicodeValue));
            }
        }
        return res_s;
    }

    int _get_code_point_at(const std::u16string& str, std::size_t index) const {
        char16_t first = str[index];
        // [0xD800,0xDBFF] is the scope of the first code unit
        if ((first >= 0xD800 && first <= 0xDBFF) && (index + 1 < str.size())) {
            char16_t second = str[index + 1];
            // [0xDC00,0xDFFF] is the scope of the second code unit
            if (second >= 0xDC00 && second <= 0xDFFF) {
                return ((first - 0xD800) << 10) + (second - 0xDC00) + 0x10000;
            }
        }

        return first;
    }
    Status _auto_partition_type_of_list(std::vector<const ColumnString::Chars*>& chars_list,
                                        std::vector<const ColumnString::Offsets*>& offsets_list,
                                        std::vector<bool>& is_const_args,
                                        const std::vector<const ColumnUInt8::Container*>& null_list,
                                        auto& res_data, auto& res_offset, size_t input_rows_count,
                                        size_t argument_size, Block& block, uint32_t result,
                                        auto& res) const {
        int curr_len = 0;
        for (int row = 0; row < input_rows_count; row++) {
            std::string res_p;
            res_p.reserve(argument_size * 5);
            res_p += 'p';
            for (int col = 1; col < argument_size; col++) {
                const auto& current_offsets = *offsets_list[col];
                const auto& current_chars = *chars_list[col];
                const auto& current_nullmap = *null_list[col];

                if (current_nullmap[row]) {
                    res_p += 'X';
                } else {
                    auto idx = index_check_const(row, is_const_args[col]);

                    int size = current_offsets[idx] - current_offsets[idx - 1];
                    const char* raw_chars =
                            reinterpret_cast<const char*>(&current_chars[current_offsets[idx - 1]]);
                    // convert string to u16string in order to convert to unicode strings
                    const std::string raw_str(raw_chars, size);
                    auto u16string = _string_to_u16string(raw_str);
                    res_p += _string_to_unicode(u16string) + std::to_string(u16string.size());
                }
            }

            // check the name of length
            int len = res_p.size();
            if (len > 50) [[unlikely]] {
                return Status::InvalidArgument(
                        "The list partition name cannot exceed 50 characters");
            }
            curr_len += len;
            res_data.resize(curr_len);
            memcpy(&res_data[res_offset[row - 1]], res_p.c_str(), len);
            res_offset[row] = res_offset[row - 1] + len;
        }
        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }

    size_t _copy_date_str_of_len_to_res_data(auto& res_data, auto& res_offset,
                                             std::vector<std::string>& date_str, size_t row,
                                             size_t len) const {
        size_t curr_len = 1;
        for (int j = 0; j < len; j++) {
            memcpy(&res_data[res_offset[row - 1]] + curr_len, date_str[j].c_str(),
                   date_str[j].size());
            curr_len += date_str[j].size();
        }
        return curr_len;
    }

    Status _auto_partition_type_of_range(std::vector<const ColumnString::Chars*>& chars_list,
                                         std::vector<const ColumnString::Offsets*>& offsets_list,
                                         std::vector<bool>& is_const_args, auto& res_data,
                                         auto& res_offset, size_t input_rows_count,
                                         size_t argument_size, Block& block, uint32_t result,
                                         auto& res) const {
        const char* range_type = chars_list[1]->raw_data();

        res_data.resize(15 * input_rows_count);
        for (int i = 0; i < input_rows_count; i++) {
            const auto& current_offsets = *offsets_list[2];
            const auto& current_chars = *chars_list[2];

            auto idx = index_check_const(i, is_const_args[2]);
            int size = current_offsets[idx] - current_offsets[idx - 1];
            const char* tmp =
                    reinterpret_cast<const char*>(&current_chars[current_offsets[idx - 1]]);
            std::string to_split_s(tmp, size);

            // check the str if it is date|datetime
            RE2 date_regex(R"(^\d{4}-\d{2}-\d{2}( \d{2}:\d{2}:\d{2})?$)");
            if (!RE2::FullMatch(to_split_s, date_regex)) {
                return Status::InvalidArgument("The range partition only support DATE|DATETIME");
            }

            // split date_str from (yyyy-mm-dd hh:mm:ss) to ([yyyy, mm, dd, hh, mm, ss])
            std::vector<std::string> date_str(6);
            date_str[0] = to_split_s.substr(0, 4);
            for (int i = 5, j = 1; i <= size; i += 3, j++) {
                date_str[j] = to_split_s.substr(i, 2);
            }
            int curr_len = 0;

            res_data[res_offset[i - 1]] = 'p';
            // raw => 2022-12-12 11:30:20
            // year => 2022 01 01 00 00 00
            // month => 2022 12 01 00 00 00
            // day => 2022 12 12 00 00 00
            // hour => 2022 12 12 11 00 00
            // minute => 2022 12  11 30 00
            // second => 2022 12 12 12 30 20

            if (!strncmp(range_type, "year", 4)) {
                curr_len += _copy_date_str_of_len_to_res_data(res_data, res_offset, date_str, i, 1);
                memcpy(&res_data[res_offset[i - 1]] + curr_len, "0101", 4);
                curr_len += 4;
            } else if (!strncmp(range_type, "month", 5)) {
                curr_len += _copy_date_str_of_len_to_res_data(res_data, res_offset, date_str, i, 2);
                memcpy(&res_data[res_offset[i - 1]] + curr_len, "01", 2);
                curr_len += 2;
            } else if (!strncmp(range_type, "day", 3)) {
                curr_len += _copy_date_str_of_len_to_res_data(res_data, res_offset, date_str, i, 3);
            } else if (!strncmp(range_type, "hour", 4)) {
                curr_len += _copy_date_str_of_len_to_res_data(res_data, res_offset, date_str, i, 4);
            } else if (!strncmp(range_type, "minute", 6)) {
                curr_len += _copy_date_str_of_len_to_res_data(res_data, res_offset, date_str, i, 5);
            } else if (!strncmp(range_type, "second", 6)) {
                curr_len += _copy_date_str_of_len_to_res_data(res_data, res_offset, date_str, i, 6);
            }

            // fill in zero
            int zero = 15 - curr_len;
            std::fill_n(&res_data[res_offset[i - 1]] + curr_len, zero, '0');
            curr_len += zero;
            res_offset[i] = res_offset[i - 1] + curr_len;
        }
        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }
};

template <typename Impl>
class FunctionSubstring : public IFunction {
public:
    static constexpr auto name = SubstringUtil::name;
    String get_name() const override { return name; }
    static FunctionPtr create() { return std::make_shared<FunctionSubstring<Impl>>(); }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }
    DataTypes get_variadic_argument_types_impl() const override {
        return Impl::get_variadic_argument_types();
    }
    size_t get_number_of_arguments() const override {
        return get_variadic_argument_types_impl().size();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        return Impl::execute_impl(context, block, arguments, result, input_rows_count);
    }
};

struct Substr3Impl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeInt32>(),
                std::make_shared<DataTypeInt32>()};
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, uint32_t result,
                               size_t input_rows_count) {
        SubstringUtil::substring_execute(block, arguments, result, input_rows_count);
        return Status::OK();
    }
};

struct Substr2Impl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeInt32>()};
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, uint32_t result,
                               size_t input_rows_count) {
        auto col_len = ColumnInt32::create(input_rows_count);
        auto& strlen_data = col_len->get_data();

        ColumnPtr str_col;
        bool str_const;
        std::tie(str_col, str_const) = unpack_if_const(block.get_by_position(arguments[0]).column);

        const auto& str_offset = assert_cast<const ColumnString*>(str_col.get())->get_offsets();

        if (str_const) {
            std::fill(strlen_data.begin(), strlen_data.end(), str_offset[0] - str_offset[-1]);
        } else {
            for (int i = 0; i < input_rows_count; ++i) {
                strlen_data[i] = str_offset[i] - str_offset[i - 1];
            }
        }

        // we complete the column2(strlen) with the default value - each row's strlen.
        block.insert({std::move(col_len), std::make_shared<DataTypeInt32>(), "strlen"});
        ColumnNumbers temp_arguments = {arguments[0], arguments[1], block.columns() - 1};

        SubstringUtil::substring_execute(block, temp_arguments, result, input_rows_count);
        return Status::OK();
    }
};

template <bool Reverse>
class FunctionMaskPartial;

class FunctionMask : public IFunction {
public:
    static constexpr auto name = "mask";
    static constexpr unsigned char DEFAULT_UPPER_MASK = 'X';
    static constexpr unsigned char DEFAULT_LOWER_MASK = 'x';
    static constexpr unsigned char DEFAULT_NUMBER_MASK = 'n';
    String get_name() const override { return name; }
    static FunctionPtr create() { return std::make_shared<FunctionMask>(); }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    size_t get_number_of_arguments() const override { return 0; }

    ColumnNumbers get_arguments_that_are_always_constant() const override { return {1, 2, 3}; }

    bool is_variadic() const override { return true; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_GE(arguments.size(), 1);
        DCHECK_LE(arguments.size(), 4);

        char upper = DEFAULT_UPPER_MASK, lower = DEFAULT_LOWER_MASK, number = DEFAULT_NUMBER_MASK;

        auto res = ColumnString::create();
        const auto& source_column =
                assert_cast<const ColumnString&>(*block.get_by_position(arguments[0]).column);

        if (arguments.size() > 1) {
            const auto& col = *block.get_by_position(arguments[1]).column;
            auto string_ref = col.get_data_at(0);
            if (string_ref.size > 0) {
                upper = *string_ref.data;
            }
        }

        if (arguments.size() > 2) {
            const auto& col = *block.get_by_position(arguments[2]).column;
            auto string_ref = col.get_data_at(0);
            if (string_ref.size > 0) {
                lower = *string_ref.data;
            }
        }

        if (arguments.size() > 3) {
            const auto& col = *block.get_by_position(arguments[3]).column;
            auto string_ref = col.get_data_at(0);
            if (string_ref.size > 0) {
                number = *string_ref.data;
            }
        }

        if (arguments.size() > 4) {
            return Status::InvalidArgument(
                    fmt::format("too many arguments for function {}", get_name()));
        }

        vector_mask(source_column, *res, upper, lower, number);

        block.get_by_position(result).column = std::move(res);

        return Status::OK();
    }
    friend class FunctionMaskPartial<true>;
    friend class FunctionMaskPartial<false>;

private:
    static void vector_mask(const ColumnString& source, ColumnString& result, const char upper,
                            const char lower, const char number) {
        result.get_chars().resize(source.get_chars().size());
        result.get_offsets().resize(source.get_offsets().size());
        memcpy_small_allow_read_write_overflow15(
                result.get_offsets().data(), source.get_offsets().data(),
                source.get_offsets().size() * sizeof(ColumnString::Offset));

        const unsigned char* src = source.get_chars().data();
        const size_t size = source.get_chars().size();
        unsigned char* res = result.get_chars().data();
        mask(src, size, upper, lower, number, res);
    }

    static void mask(const unsigned char* __restrict src, const size_t size,
                     const unsigned char upper, const unsigned char lower,
                     const unsigned char number, unsigned char* __restrict res) {
        for (size_t i = 0; i != size; ++i) {
            auto c = src[i];
            if (c >= 'A' && c <= 'Z') {
                res[i] = upper;
            } else if (c >= 'a' && c <= 'z') {
                res[i] = lower;
            } else if (c >= '0' && c <= '9') {
                res[i] = number;
            } else {
                res[i] = c;
            }
        }
    }
};

template <bool Reverse>
class FunctionMaskPartial : public IFunction {
public:
    static constexpr auto name = Reverse ? "mask_last_n" : "mask_first_n";
    String get_name() const override { return name; }
    static FunctionPtr create() { return std::make_shared<FunctionMaskPartial>(); }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    size_t get_number_of_arguments() const override { return 0; }

    bool is_variadic() const override { return true; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        int n = -1; // means unassigned

        auto res = ColumnString::create();
        auto col = block.get_by_position(arguments[0]).column->convert_to_full_column_if_const();
        const auto& source_column = assert_cast<const ColumnString&>(*col);

        if (arguments.size() == 2) {
            const auto& col = *block.get_by_position(arguments[1]).column;
            // the 2nd arg is const. checked in fe.
            if (col.get_int(0) < 0) [[unlikely]] {
                return Status::InvalidArgument(
                        "function {} only accept non-negative input for 2nd argument but got {}",
                        name, col.get_int(0));
            }
            n = col.get_int(0);
        }

        if (n == -1) { // no 2nd arg, just mask all
            FunctionMask::vector_mask(source_column, *res, FunctionMask::DEFAULT_UPPER_MASK,
                                      FunctionMask::DEFAULT_LOWER_MASK,
                                      FunctionMask::DEFAULT_NUMBER_MASK);
        } else { // n >= 0
            vector(source_column, n, *res);
        }

        block.get_by_position(result).column = std::move(res);

        return Status::OK();
    }

private:
    static void vector(const ColumnString& src, int n, ColumnString& result) {
        const auto num_rows = src.size();
        const auto* chars = src.get_chars().data();
        const auto* offsets = src.get_offsets().data();
        result.get_chars().resize(src.get_chars().size());
        result.get_offsets().resize(src.get_offsets().size());
        memcpy_small_allow_read_write_overflow15(
                result.get_offsets().data(), src.get_offsets().data(),
                src.get_offsets().size() * sizeof(ColumnString::Offset));
        auto* res = result.get_chars().data();

        for (ssize_t i = 0; i != num_rows; ++i) {
            auto offset = offsets[i - 1];
            int len = offsets[i] - offset;
            if constexpr (Reverse) {
                auto start = std::max(len - n, 0);
                if (start > 0) {
                    memcpy(&res[offset], &chars[offset], start);
                }
                offset += start;
            } else {
                if (n < len) {
                    memcpy(&res[offset + n], &chars[offset + n], len - n);
                }
            }

            len = std::min(n, len);
            FunctionMask::mask(&chars[offset], len, FunctionMask::DEFAULT_UPPER_MASK,
                               FunctionMask::DEFAULT_LOWER_MASK, FunctionMask::DEFAULT_NUMBER_MASK,
                               &res[offset]);
        }
    }
};

class FunctionLeft : public IFunction {
public:
    static constexpr auto name = "left";
    static FunctionPtr create() { return std::make_shared<FunctionLeft>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 2; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto int_type = std::make_shared<DataTypeInt32>();
        size_t num_columns_without_result = block.columns();
        block.insert({int_type->create_column_const(input_rows_count, to_field<TYPE_INT>(1)),
                      int_type, "const 1"});
        ColumnNumbers temp_arguments(3);
        temp_arguments[0] = arguments[0];
        temp_arguments[1] = num_columns_without_result;
        temp_arguments[2] = arguments[1];

        SubstringUtil::substring_execute(block, temp_arguments, result, input_rows_count);
        return Status::OK();
    }
};

class FunctionRight : public IFunction {
public:
    static constexpr auto name = "right";
    static FunctionPtr create() { return std::make_shared<FunctionRight>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 2; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto int_type = std::make_shared<DataTypeInt32>();
        auto params1 = ColumnInt32::create(input_rows_count);
        auto params2 = ColumnInt32::create(input_rows_count);
        size_t num_columns_without_result = block.columns();

        // params1 = max(arg[1], -len(arg))
        auto& index_data = params1->get_data();
        auto& strlen_data = params2->get_data();

        auto str_col =
                block.get_by_position(arguments[0]).column->convert_to_full_column_if_const();
        const auto* str_column = assert_cast<const ColumnString*>(str_col.get());
        auto pos_col =
                block.get_by_position(arguments[1]).column->convert_to_full_column_if_const();
        const auto& pos_data = assert_cast<const ColumnInt32*>(pos_col.get())->get_data();

        for (int i = 0; i < input_rows_count; ++i) {
            auto str = str_column->get_data_at(i);
            strlen_data[i] = simd::VStringFunctions::get_char_len(str.data, str.size);
        }

        for (int i = 0; i < input_rows_count; ++i) {
            index_data[i] = std::max(-pos_data[i], -strlen_data[i]);
        }

        block.insert({std::move(params1), int_type, "index"});
        block.insert({std::move(params2), int_type, "strlen"});

        ColumnNumbers temp_arguments(3);
        temp_arguments[0] = arguments[0];
        temp_arguments[1] = num_columns_without_result;
        temp_arguments[2] = num_columns_without_result + 1;
        SubstringUtil::substring_execute(block, temp_arguments, result, input_rows_count);
        return Status::OK();
    }
};

struct NullOrEmptyImpl {
    static DataTypes get_variadic_argument_types() { return {std::make_shared<DataTypeUInt8>()}; }

    static Status execute(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                          uint32_t result, size_t input_rows_count, bool reverse) {
        auto res_map = ColumnUInt8::create(input_rows_count, 0);

        auto column = block.get_by_position(arguments[0]).column;
        if (auto* nullable = check_and_get_column<const ColumnNullable>(*column)) {
            column = nullable->get_nested_column_ptr();
            VectorizedUtils::update_null_map(res_map->get_data(), nullable->get_null_map_data());
        }
        auto str_col = assert_cast<const ColumnString*>(column.get());
        const auto& offsets = str_col->get_offsets();

        auto& res_map_data = res_map->get_data();
        for (int i = 0; i < input_rows_count; ++i) {
            int size = offsets[i] - offsets[i - 1];
            res_map_data[i] |= (size == 0);
        }
        if (reverse) {
            for (int i = 0; i < input_rows_count; ++i) {
                res_map_data[i] = !res_map_data[i];
            }
        }

        block.replace_by_position(result, std::move(res_map));
        return Status::OK();
    }
};

class FunctionNullOrEmpty : public IFunction {
public:
    static constexpr auto name = "null_or_empty";
    static FunctionPtr create() { return std::make_shared<FunctionNullOrEmpty>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 1; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeUInt8>();
    }

    bool use_default_implementation_for_nulls() const override { return false; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        RETURN_IF_ERROR(NullOrEmptyImpl::execute(context, block, arguments, result,
                                                 input_rows_count, false));
        return Status::OK();
    }
};

class FunctionNotNullOrEmpty : public IFunction {
public:
    static constexpr auto name = "not_null_or_empty";
    static FunctionPtr create() { return std::make_shared<FunctionNotNullOrEmpty>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 1; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeUInt8>();
    }

    bool use_default_implementation_for_nulls() const override { return false; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        RETURN_IF_ERROR(NullOrEmptyImpl::execute(context, block, arguments, result,
                                                 input_rows_count, true));
        return Status::OK();
    }
};

class FunctionStringConcat : public IFunction {
public:
    struct ConcatState {
        bool use_state = false;
        std::string tail;
    };

    static constexpr auto name = "concat";
    static FunctionPtr create() { return std::make_shared<FunctionStringConcat>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status open(FunctionContext* context, FunctionContext::FunctionStateScope scope) override {
        if (scope == FunctionContext::THREAD_LOCAL) {
            return Status::OK();
        }
        std::shared_ptr<ConcatState> state = std::make_shared<ConcatState>();

        context->set_function_state(scope, state);

        state->use_state = true;

        // Optimize function calls like this:
        // concat(col, "123", "abc", "456") -> tail = "123abc456"
        for (size_t i = 1; i < context->get_num_args(); i++) {
            const auto* column_string = context->get_constant_col(i);
            if (column_string == nullptr) {
                state->use_state = false;
                return IFunction::open(context, scope);
            }
            auto string_vale = column_string->column_ptr->get_data_at(0);
            if (string_vale.data == nullptr) {
                // For concat(col, null), it is handled by default_implementation_for_nulls
                state->use_state = false;
                return IFunction::open(context, scope);
            }

            state->tail.append(string_vale.begin(), string_vale.size);
        }

        // The reserve is used here to allow the usage of memcpy_small_allow_read_write_overflow15 below.
        state->tail.reserve(state->tail.size() + 16);

        return IFunction::open(context, scope);
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_GE(arguments.size(), 1);

        if (arguments.size() == 1) {
            block.get_by_position(result).column = block.get_by_position(arguments[0]).column;
            return Status::OK();
        }
        auto* concat_state = reinterpret_cast<ConcatState*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        if (!concat_state) {
            return Status::RuntimeError("funciton context for function '{}' must have ConcatState;",
                                        get_name());
        }
        if (concat_state->use_state) {
            const auto& [col, is_const] =
                    unpack_if_const(block.get_by_position(arguments[0]).column);
            const auto* col_str = assert_cast<const ColumnString*>(col.get());
            if (is_const) {
                return execute_const<true>(concat_state, block, col_str, result, input_rows_count);
            } else {
                return execute_const<false>(concat_state, block, col_str, result, input_rows_count);
            }

        } else {
            return execute_vecotr(block, arguments, result, input_rows_count);
        }
    }

    Status execute_vecotr(Block& block, const ColumnNumbers& arguments, uint32_t result,
                          size_t input_rows_count) const {
        int argument_size = arguments.size();
        std::vector<ColumnPtr> argument_columns(argument_size);

        std::vector<const ColumnString::Offsets*> offsets_list(argument_size);
        std::vector<const ColumnString::Chars*> chars_list(argument_size);
        std::vector<bool> is_const_args(argument_size);

        for (int i = 0; i < argument_size; ++i) {
            const auto& [col, is_const] =
                    unpack_if_const(block.get_by_position(arguments[i]).column);

            const auto* col_str = assert_cast<const ColumnString*>(col.get());
            offsets_list[i] = &col_str->get_offsets();
            chars_list[i] = &col_str->get_chars();
            is_const_args[i] = is_const;
        }

        auto res = ColumnString::create();
        auto& res_data = res->get_chars();
        auto& res_offset = res->get_offsets();

        res_offset.resize(input_rows_count);
        size_t res_reserve_size = 0;
        for (size_t i = 0; i < argument_size; ++i) {
            if (is_const_args[i]) {
                res_reserve_size += (*offsets_list[i])[0] * input_rows_count;
            } else {
                res_reserve_size += (*offsets_list[i])[input_rows_count - 1];
            }
        }

        ColumnString::check_chars_length(res_reserve_size, 0);

        res_data.resize(res_reserve_size);

        auto* data = res_data.data();
        size_t dst_offset = 0;

        for (size_t i = 0; i < input_rows_count; ++i) {
            for (size_t j = 0; j < argument_size; ++j) {
                const auto& current_offsets = *offsets_list[j];
                const auto& current_chars = *chars_list[j];
                auto idx = index_check_const(i, is_const_args[j]);
                const auto size = current_offsets[idx] - current_offsets[idx - 1];
                if (size > 0) {
                    memcpy_small_allow_read_write_overflow15(
                            data + dst_offset, current_chars.data() + current_offsets[idx - 1],
                            size);
                    dst_offset += size;
                }
            }
            res_offset[i] = dst_offset;
        }

        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }

    template <bool is_const>
    Status execute_const(ConcatState* concat_state, Block& block, const ColumnString* col_str,
                         uint32_t result, size_t input_rows_count) const {
        // using tail optimize

        auto res = ColumnString::create();
        auto& res_data = res->get_chars();
        auto& res_offset = res->get_offsets();
        res_offset.resize(input_rows_count);

        size_t res_reserve_size = 0;
        if constexpr (is_const) {
            res_reserve_size = col_str->get_offsets()[0] * input_rows_count;
        } else {
            res_reserve_size = col_str->get_offsets()[input_rows_count - 1];
        }
        res_reserve_size += concat_state->tail.size() * input_rows_count;

        ColumnString::check_chars_length(res_reserve_size, 0);
        res_data.resize(res_reserve_size);

        const auto& tail = concat_state->tail;
        auto* data = res_data.data();
        size_t dst_offset = 0;

        for (size_t i = 0; i < input_rows_count; ++i) {
            const auto idx = index_check_const<is_const>(i);
            StringRef str_val = col_str->get_data_at(idx);
            // copy column
            memcpy_small_allow_read_write_overflow15(data + dst_offset, str_val.data, str_val.size);
            dst_offset += str_val.size;
            // copy tail
            memcpy_small_allow_read_write_overflow15(data + dst_offset, tail.data(), tail.size());
            dst_offset += tail.size();
            res_offset[i] = dst_offset;
        }
        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }
};

class FunctionStringElt : public IFunction {
public:
    static constexpr auto name = "elt";
    static FunctionPtr create() { return std::make_shared<FunctionStringElt>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }
    bool use_default_implementation_for_nulls() const override { return false; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        int arguent_size = arguments.size();
        int num_children = arguent_size - 1;
        auto res = ColumnString::create();

        if (auto const_column = check_and_get_column<ColumnConst>(
                    *block.get_by_position(arguments[0]).column)) {
            auto data = const_column->get_data_at(0);
            // return NULL, pos is null or pos < 0 or pos > num_children
            auto is_null = data.data == nullptr;
            auto pos = is_null ? 0 : *(Int32*)data.data;
            is_null = pos <= 0 || pos > num_children;

            auto null_map = ColumnUInt8::create(input_rows_count, is_null);
            if (is_null) {
                res->insert_many_defaults(input_rows_count);
            } else {
                auto& target_column = block.get_by_position(arguments[pos]).column;
                if (auto target_const_column = check_and_get_column<ColumnConst>(*target_column)) {
                    auto target_data = target_const_column->get_data_at(0);
                    // return NULL, no target data
                    if (target_data.data == nullptr) {
                        null_map = ColumnUInt8::create(input_rows_count, true);
                        res->insert_many_defaults(input_rows_count);
                    } else {
                        res->insert_data_repeatedly(target_data.data, target_data.size,
                                                    input_rows_count);
                    }
                } else if (auto target_nullable_column =
                                   check_and_get_column<ColumnNullable>(*target_column)) {
                    auto& target_null_map = target_nullable_column->get_null_map_data();
                    VectorizedUtils::update_null_map(
                            assert_cast<ColumnUInt8&>(*null_map).get_data(), target_null_map);

                    auto& target_str_column = assert_cast<const ColumnString&>(
                            target_nullable_column->get_nested_column());
                    res->get_chars().assign(target_str_column.get_chars().begin(),
                                            target_str_column.get_chars().end());
                    res->get_offsets().assign(target_str_column.get_offsets().begin(),
                                              target_str_column.get_offsets().end());
                } else {
                    auto& target_str_column = assert_cast<const ColumnString&>(*target_column);
                    res->get_chars().assign(target_str_column.get_chars().begin(),
                                            target_str_column.get_chars().end());
                    res->get_offsets().assign(target_str_column.get_offsets().begin(),
                                              target_str_column.get_offsets().end());
                }
            }
            block.get_by_position(result).column =
                    ColumnNullable::create(std::move(res), std::move(null_map));
        } else if (auto pos_null_column = check_and_get_column<ColumnNullable>(
                           *block.get_by_position(arguments[0]).column)) {
            auto& pos_column =
                    assert_cast<const ColumnInt32&>(pos_null_column->get_nested_column());
            auto& pos_null_map = pos_null_column->get_null_map_data();
            auto null_map = ColumnUInt8::create(input_rows_count, false);
            auto& res_null_map = assert_cast<ColumnUInt8&>(*null_map).get_data();

            for (size_t i = 0; i < input_rows_count; ++i) {
                auto pos = pos_column.get_element(i);
                res_null_map[i] =
                        pos_null_map[i] || pos <= 0 || pos > num_children ||
                        block.get_by_position(arguments[pos]).column->get_data_at(i).data ==
                                nullptr;
                if (res_null_map[i]) {
                    res->insert_default();
                } else {
                    auto insert_data = block.get_by_position(arguments[pos]).column->get_data_at(i);
                    res->insert_data(insert_data.data, insert_data.size);
                }
            }
            block.get_by_position(result).column =
                    ColumnNullable::create(std::move(res), std::move(null_map));
        } else {
            auto& pos_column =
                    assert_cast<const ColumnInt32&>(*block.get_by_position(arguments[0]).column);
            auto null_map = ColumnUInt8::create(input_rows_count, false);
            auto& res_null_map = assert_cast<ColumnUInt8&>(*null_map).get_data();

            for (size_t i = 0; i < input_rows_count; ++i) {
                auto pos = pos_column.get_element(i);
                res_null_map[i] =
                        pos <= 0 || pos > num_children ||
                        block.get_by_position(arguments[pos]).column->get_data_at(i).data ==
                                nullptr;
                if (res_null_map[i]) {
                    res->insert_default();
                } else {
                    auto insert_data = block.get_by_position(arguments[pos]).column->get_data_at(i);
                    res->insert_data(insert_data.data, insert_data.size);
                }
            }
            block.get_by_position(result).column =
                    ColumnNullable::create(std::move(res), std::move(null_map));
        }
        return Status::OK();
    }
};

// concat_ws (string,string....) or (string, Array)
// TODO: avoid use fmtlib
class FunctionStringConcatWs : public IFunction {
public:
    using Chars = ColumnString::Chars;
    using Offsets = ColumnString::Offsets;

    static constexpr auto name = "concat_ws";
    static FunctionPtr create() { return std::make_shared<FunctionStringConcatWs>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        const IDataType* first_type = arguments[0].get();
        if (first_type->is_nullable()) {
            return make_nullable(std::make_shared<DataTypeString>());
        } else {
            return std::make_shared<DataTypeString>();
        }
    }
    bool use_default_implementation_for_nulls() const override { return false; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_GE(arguments.size(), 2);
        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        // we create a zero column to simply implement
        auto const_null_map = ColumnUInt8::create(input_rows_count, 0);
        auto res = ColumnString::create();
        bool is_null_type = block.get_by_position(arguments[0]).type.get()->is_nullable();
        size_t argument_size = arguments.size();
        std::vector<const Offsets*> offsets_list(argument_size);
        std::vector<const Chars*> chars_list(argument_size);
        std::vector<const ColumnUInt8::Container*> null_list(argument_size);

        std::vector<ColumnPtr> argument_columns(argument_size);
        std::vector<ColumnPtr> argument_null_columns(argument_size);

        for (size_t i = 0; i < argument_size; ++i) {
            argument_columns[i] =
                    block.get_by_position(arguments[i]).column->convert_to_full_column_if_const();
            if (const auto* nullable =
                        check_and_get_column<const ColumnNullable>(*argument_columns[i])) {
                // Danger: Here must dispose the null map data first! Because
                // argument_columns[i]=nullable->get_nested_column_ptr(); will release the mem
                // of column nullable mem of null map
                null_list[i] = &nullable->get_null_map_data();
                argument_null_columns[i] = nullable->get_null_map_column_ptr();
                argument_columns[i] = nullable->get_nested_column_ptr();
            } else {
                null_list[i] = &const_null_map->get_data();
            }

            if (is_column<ColumnArray>(argument_columns[i].get())) {
                continue;
            }

            const auto* col_str = assert_cast<const ColumnString*>(argument_columns[i].get());
            offsets_list[i] = &col_str->get_offsets();
            chars_list[i] = &col_str->get_chars();
        }

        auto& res_data = res->get_chars();
        auto& res_offset = res->get_offsets();
        res_offset.resize(input_rows_count);

        VectorizedUtils::update_null_map(null_map->get_data(), *null_list[0]);
        fmt::memory_buffer buffer;
        std::vector<std::string_view> views;

        if (is_column<ColumnArray>(argument_columns[1].get())) {
            // Determine if the nested type of the array is String
            const auto& array_column = reinterpret_cast<const ColumnArray&>(*argument_columns[1]);
            if (!array_column.get_data().is_column_string()) {
                return Status::NotSupported(
                        fmt::format("unsupported nested array of type {} for function {}",
                                    is_column_nullable(array_column.get_data())
                                            ? array_column.get_data().get_name()
                                            : array_column.get_data().get_name(),
                                    get_name()));
            }
            // Concat string in array
            _execute_array(input_rows_count, array_column, buffer, views, offsets_list, chars_list,
                           null_list, res_data, res_offset);

        } else {
            // Concat string
            _execute_string(input_rows_count, argument_size, buffer, views, offsets_list,
                            chars_list, null_list, res_data, res_offset);
        }
        if (is_null_type) {
            block.get_by_position(result).column =
                    ColumnNullable::create(std::move(res), std::move(null_map));
        } else {
            block.get_by_position(result).column = std::move(res);
        }
        return Status::OK();
    }

private:
    void _execute_array(const size_t& input_rows_count, const ColumnArray& array_column,
                        fmt::memory_buffer& buffer, std::vector<std::string_view>& views,
                        const std::vector<const Offsets*>& offsets_list,
                        const std::vector<const Chars*>& chars_list,
                        const std::vector<const ColumnUInt8::Container*>& null_list,
                        Chars& res_data, Offsets& res_offset) const {
        // Get array nested column
        const UInt8* array_nested_null_map = nullptr;
        ColumnPtr array_nested_column = nullptr;

        if (is_column_nullable(array_column.get_data())) {
            const auto& array_nested_null_column =
                    reinterpret_cast<const ColumnNullable&>(array_column.get_data());
            // String's null map in array
            array_nested_null_map =
                    array_nested_null_column.get_null_map_column().get_data().data();
            array_nested_column = array_nested_null_column.get_nested_column_ptr();
        } else {
            array_nested_column = array_column.get_data_ptr();
        }

        const auto& string_column = reinterpret_cast<const ColumnString&>(*array_nested_column);
        const Chars& string_src_chars = string_column.get_chars();
        const auto& src_string_offsets = string_column.get_offsets();
        const auto& src_array_offsets = array_column.get_offsets();
        size_t current_src_array_offset = 0;

        // Concat string in array
        for (size_t i = 0; i < input_rows_count; ++i) {
            auto& sep_offsets = *offsets_list[0];
            auto& sep_chars = *chars_list[0];
            auto& sep_nullmap = *null_list[0];

            if (sep_nullmap[i]) {
                res_offset[i] = res_data.size();
                current_src_array_offset += src_array_offsets[i] - src_array_offsets[i - 1];
                continue;
            }

            int sep_size = sep_offsets[i] - sep_offsets[i - 1];
            const char* sep_data = reinterpret_cast<const char*>(&sep_chars[sep_offsets[i - 1]]);

            std::string_view sep(sep_data, sep_size);
            buffer.clear();
            views.clear();

            for (auto next_src_array_offset = src_array_offsets[i];
                 current_src_array_offset < next_src_array_offset; ++current_src_array_offset) {
                const auto current_src_string_offset =
                        current_src_array_offset ? src_string_offsets[current_src_array_offset - 1]
                                                 : 0;
                size_t bytes_to_copy =
                        src_string_offsets[current_src_array_offset] - current_src_string_offset;
                const char* ptr =
                        reinterpret_cast<const char*>(&string_src_chars[current_src_string_offset]);

                if (array_nested_null_map == nullptr ||
                    !array_nested_null_map[current_src_array_offset]) {
                    views.emplace_back(ptr, bytes_to_copy);
                }
            }

            fmt::format_to(buffer, "{}", fmt::join(views, sep));

            StringOP::push_value_string(std::string_view(buffer.data(), buffer.size()), i, res_data,
                                        res_offset);
        }
    }

    void _execute_string(const size_t& input_rows_count, const size_t& argument_size,
                         fmt::memory_buffer& buffer, std::vector<std::string_view>& views,
                         const std::vector<const Offsets*>& offsets_list,
                         const std::vector<const Chars*>& chars_list,
                         const std::vector<const ColumnUInt8::Container*>& null_list,
                         Chars& res_data, Offsets& res_offset) const {
        // Concat string
        for (size_t i = 0; i < input_rows_count; ++i) {
            auto& sep_offsets = *offsets_list[0];
            auto& sep_chars = *chars_list[0];
            auto& sep_nullmap = *null_list[0];
            if (sep_nullmap[i]) {
                res_offset[i] = res_data.size();
                continue;
            }

            int sep_size = sep_offsets[i] - sep_offsets[i - 1];
            const char* sep_data = reinterpret_cast<const char*>(&sep_chars[sep_offsets[i - 1]]);

            std::string_view sep(sep_data, sep_size);
            buffer.clear();
            views.clear();
            for (size_t j = 1; j < argument_size; ++j) {
                auto& current_offsets = *offsets_list[j];
                auto& current_chars = *chars_list[j];
                auto& current_nullmap = *null_list[j];
                int size = current_offsets[i] - current_offsets[i - 1];
                const char* ptr =
                        reinterpret_cast<const char*>(&current_chars[current_offsets[i - 1]]);
                if (!current_nullmap[i]) {
                    views.emplace_back(ptr, size);
                }
            }
            fmt::format_to(buffer, "{}", fmt::join(views, sep));
            StringOP::push_value_string(std::string_view(buffer.data(), buffer.size()), i, res_data,
                                        res_offset);
        }
    }
};

class FunctionStringRepeat : public IFunction {
public:
    static constexpr auto name = "repeat";
    static FunctionPtr create() { return std::make_shared<FunctionStringRepeat>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 2; }
    // should set NULL value of nested data to default,
    // as iff it's not inited and invalid, the repeat result of length is so large cause overflow
    bool need_replace_null_data_to_default() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }
    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 2);
        auto res = ColumnString::create();
        auto null_map = ColumnUInt8::create();

        ColumnPtr argument_ptr[2];
        argument_ptr[0] =
                block.get_by_position(arguments[0]).column->convert_to_full_column_if_const();
        argument_ptr[1] = block.get_by_position(arguments[1]).column;

        if (const auto* col1 = check_and_get_column<ColumnString>(*argument_ptr[0])) {
            if (const auto* col2 = check_and_get_column<ColumnInt32>(*argument_ptr[1])) {
                RETURN_IF_ERROR(vector_vector(col1->get_chars(), col1->get_offsets(),
                                              col2->get_data(), res->get_chars(),
                                              res->get_offsets(), null_map->get_data()));
                block.replace_by_position(
                        result, ColumnNullable::create(std::move(res), std::move(null_map)));
                return Status::OK();
            } else if (const auto* col2_const =
                               check_and_get_column<ColumnConst>(*argument_ptr[1])) {
                DCHECK(check_and_get_column<ColumnInt32>(col2_const->get_data_column()));
                int repeat = col2_const->get_int(0);
                if (repeat <= 0) {
                    null_map->get_data().resize_fill(input_rows_count, 0);
                    res->insert_many_defaults(input_rows_count);
                } else {
                    vector_const(col1->get_chars(), col1->get_offsets(), repeat, res->get_chars(),
                                 res->get_offsets(), null_map->get_data());
                }
                block.replace_by_position(
                        result, ColumnNullable::create(std::move(res), std::move(null_map)));
                return Status::OK();
            }
        }

        return Status::RuntimeError("repeat function get error param: {}, {}",
                                    argument_ptr[0]->get_name(), argument_ptr[1]->get_name());
    }

    Status vector_vector(const ColumnString::Chars& data, const ColumnString::Offsets& offsets,
                         const ColumnInt32::Container& repeats, ColumnString::Chars& res_data,
                         ColumnString::Offsets& res_offsets,
                         ColumnUInt8::Container& null_map) const {
        size_t input_row_size = offsets.size();

        fmt::memory_buffer buffer;
        res_offsets.resize(input_row_size);
        null_map.resize_fill(input_row_size, 0);
        for (ssize_t i = 0; i < input_row_size; ++i) {
            buffer.clear();
            const char* raw_str = reinterpret_cast<const char*>(&data[offsets[i - 1]]);
            size_t size = offsets[i] - offsets[i - 1];
            int repeat = repeats[i];
            if (repeat <= 0) {
                StringOP::push_empty_string(i, res_data, res_offsets);
            } else {
                ColumnString::check_chars_length(repeat * size + res_data.size(), 0);
                for (int j = 0; j < repeat; ++j) {
                    buffer.append(raw_str, raw_str + size);
                }
                StringOP::push_value_string(std::string_view(buffer.data(), buffer.size()), i,
                                            res_data, res_offsets);
            }
        }
        return Status::OK();
    }

    // TODO: 1. use pmr::vector<char> replace fmt_buffer may speed up the code
    //       2. abstract the `vector_vector` and `vector_const`
    //       3. rethink we should use `DEFAULT_MAX_STRING_SIZE` to bigger here
    void vector_const(const ColumnString::Chars& data, const ColumnString::Offsets& offsets,
                      int repeat, ColumnString::Chars& res_data, ColumnString::Offsets& res_offsets,
                      ColumnUInt8::Container& null_map) const {
        size_t input_row_size = offsets.size();

        fmt::memory_buffer buffer;
        res_offsets.resize(input_row_size);
        null_map.resize_fill(input_row_size, 0);
        for (ssize_t i = 0; i < input_row_size; ++i) {
            buffer.clear();
            const char* raw_str = reinterpret_cast<const char*>(&data[offsets[i - 1]]);
            size_t size = offsets[i] - offsets[i - 1];
            ColumnString::check_chars_length(repeat * size + res_data.size(), 0);

            for (int j = 0; j < repeat; ++j) {
                buffer.append(raw_str, raw_str + size);
            }
            StringOP::push_value_string(std::string_view(buffer.data(), buffer.size()), i, res_data,
                                        res_offsets);
        }
    }
};

template <typename Impl>
class FunctionStringPad : public IFunction {
public:
    static constexpr auto name = Impl::name;
    static FunctionPtr create() { return std::make_shared<FunctionStringPad>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_GE(arguments.size(), 3);
        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        // we create a zero column to simply implement
        auto const_null_map = ColumnUInt8::create(input_rows_count, 0);
        auto res = ColumnString::create();

        ColumnPtr col[3];
        bool col_const[3];
        for (size_t i = 0; i < 3; ++i) {
            std::tie(col[i], col_const[i]) =
                    unpack_if_const(block.get_by_position(arguments[i]).column);
        }
        auto& null_map_data = null_map->get_data();
        auto& res_offsets = res->get_offsets();
        auto& res_chars = res->get_chars();
        res_offsets.resize(input_rows_count);

        const auto* strcol = assert_cast<const ColumnString*>(col[0].get());
        const auto& strcol_offsets = strcol->get_offsets();
        const auto& strcol_chars = strcol->get_chars();

        const auto* col_len = assert_cast<const ColumnInt32*>(col[1].get());
        const auto& col_len_data = col_len->get_data();

        const auto* padcol = assert_cast<const ColumnString*>(col[2].get());
        const auto& padcol_offsets = padcol->get_offsets();
        const auto& padcol_chars = padcol->get_chars();
        std::visit(
                [&](auto str_const, auto len_const, auto pad_const) {
                    execute_utf8<str_const, len_const, pad_const>(
                            strcol_offsets, strcol_chars, col_len_data, padcol_offsets,
                            padcol_chars, res_offsets, res_chars, null_map_data, input_rows_count);
                },
                vectorized::make_bool_variant(col_const[0]),
                vectorized::make_bool_variant(col_const[1]),
                vectorized::make_bool_variant(col_const[2]));

        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res), std::move(null_map));
        return Status::OK();
    }

    template <bool str_const, bool len_const, bool pad_const>
    void execute_utf8(const ColumnString::Offsets& strcol_offsets,
                      const ColumnString::Chars& strcol_chars,
                      const ColumnInt32::Container& col_len_data,
                      const ColumnString::Offsets& padcol_offsets,
                      const ColumnString::Chars& padcol_chars, ColumnString::Offsets& res_offsets,
                      ColumnString::Chars& res_chars, ColumnUInt8::Container& null_map_data,
                      size_t input_rows_count) const {
        std::vector<size_t> pad_index;
        size_t const_pad_char_size = 0;
        // If pad_const = true, initialize pad_index only once.
        // The same logic applies to the if constexpr (!pad_const) condition below.
        if constexpr (pad_const) {
            const_pad_char_size = simd::VStringFunctions::get_char_len(
                    (const char*)padcol_chars.data(), padcol_offsets[0], pad_index);
        }

        fmt::memory_buffer buffer;
        buffer.resize(strcol_chars.size());
        size_t buffer_len = 0;

        for (size_t i = 0; i < input_rows_count; ++i) {
            if constexpr (!pad_const) {
                pad_index.clear();
            }
            const auto len = col_len_data[index_check_const<len_const>(i)];
            if (len < 0) {
                // return NULL when input length is invalid number
                null_map_data[i] = true;
                res_offsets[i] = buffer_len;
            } else {
                const auto str_idx = index_check_const<str_const>(i);
                const int str_len = strcol_offsets[str_idx] - strcol_offsets[str_idx - 1];
                const auto* str_data = &strcol_chars[strcol_offsets[str_idx - 1]];
                const auto pad_idx = index_check_const<pad_const>(i);
                const int pad_len = padcol_offsets[pad_idx] - padcol_offsets[pad_idx - 1];
                const auto* pad_data = &padcol_chars[padcol_offsets[pad_idx - 1]];

                auto [iterate_byte_len, iterate_char_len] =
                        simd::VStringFunctions::iterate_utf8_with_limit_length(
                                (const char*)str_data, (const char*)str_data + str_len, len);
                // If iterate_char_len equals len, it indicates that the str length is greater than or equal to len
                if (iterate_char_len == len) {
                    buffer.resize(buffer_len + iterate_byte_len);
                    memcpy(buffer.data() + buffer_len, str_data, iterate_byte_len);
                    buffer_len += iterate_byte_len;
                    res_offsets[i] = buffer_len;
                    continue;
                }
                size_t pad_char_size;
                if constexpr (!pad_const) {
                    pad_char_size = simd::VStringFunctions::get_char_len((const char*)pad_data,
                                                                         pad_len, pad_index);
                } else {
                    pad_char_size = const_pad_char_size;
                }

                // make compatible with mysql. return empty string if pad is empty
                if (pad_char_size == 0) {
                    res_offsets[i] = buffer_len;
                    continue;
                }
                const size_t str_char_size = iterate_char_len;
                const size_t pad_times = (len - str_char_size) / pad_char_size;
                const size_t pad_remainder_len = pad_index[(len - str_char_size) % pad_char_size];
                const size_t new_capacity = str_len + size_t(pad_times + 1) * pad_len;
                ColumnString::check_chars_length(buffer_len + new_capacity, i);
                buffer.resize(buffer_len + new_capacity);
                if constexpr (!Impl::is_lpad) {
                    memcpy(buffer.data() + buffer_len, str_data, str_len);
                    buffer_len += str_len;
                }
                // Prepend chars of pad.
                StringOP::fast_repeat((uint8_t*)buffer.data() + buffer_len, pad_data, pad_len,
                                      pad_times);
                buffer_len += pad_times * pad_len;

                memcpy(buffer.data() + buffer_len, pad_data, pad_remainder_len);
                buffer_len += pad_remainder_len;

                if constexpr (Impl::is_lpad) {
                    memcpy(buffer.data() + buffer_len, str_data, str_len);
                    buffer_len += str_len;
                }
                res_offsets[i] = buffer_len;
            }
        }
        res_chars.insert(buffer.data(), buffer.data() + buffer_len);
    }
};

template <typename Impl>
class FunctionStringFormatRound : public IFunction {
public:
    static constexpr auto name = "format_round";
    static FunctionPtr create() { return std::make_shared<FunctionStringFormatRound>(); }
    String get_name() const override { return name; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        if (arguments.size() != 2) {
            throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                   "Function {} requires exactly 2 argument", name);
        }
        return std::make_shared<DataTypeString>();
    }
    DataTypes get_variadic_argument_types_impl() const override {
        return Impl::get_variadic_argument_types();
    }
    size_t get_number_of_arguments() const override { return 2; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto res_column = ColumnString::create();
        ColumnPtr argument_column = block.get_by_position(arguments[0]).column;
        ColumnPtr argument_column_2;
        bool is_const;
        std::tie(argument_column_2, is_const) =
                unpack_if_const(block.get_by_position(arguments[1]).column);

        auto result_column = assert_cast<ColumnString*>(res_column.get());

        RETURN_IF_ERROR(Impl::execute(context, result_column, argument_column, argument_column_2,
                                      input_rows_count));

        block.replace_by_position(result, std::move(res_column));
        return Status::OK();
    }
};

class FunctionSplitPart : public IFunction {
public:
    static constexpr auto name = "split_part";
    static FunctionPtr create() { return std::make_shared<FunctionSplitPart>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 3);

        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        // Create a zero column to simply implement
        auto const_null_map = ColumnUInt8::create(input_rows_count, 0);
        auto res = ColumnString::create();

        auto& null_map_data = null_map->get_data();
        auto& res_offsets = res->get_offsets();
        auto& res_chars = res->get_chars();
        res_offsets.resize(input_rows_count);

        const size_t argument_size = arguments.size();
        std::vector<ColumnPtr> argument_columns(argument_size);
        for (size_t i = 0; i < argument_size; ++i) {
            argument_columns[i] =
                    block.get_by_position(arguments[i]).column->convert_to_full_column_if_const();
            if (const auto* nullable =
                        check_and_get_column<const ColumnNullable>(*argument_columns[i])) {
                // Danger: Here must dispose the null map data first! Because
                // argument_columns[i]=nullable->get_nested_column_ptr(); will release the mem
                // of column nullable mem of null map
                VectorizedUtils::update_null_map(null_map->get_data(),
                                                 nullable->get_null_map_data());
                argument_columns[i] = nullable->get_nested_column_ptr();
            }
        }

        const auto* str_col = assert_cast<const ColumnString*>(argument_columns[0].get());

        const auto* delimiter_col = assert_cast<const ColumnString*>(argument_columns[1].get());

        const auto* part_num_col = assert_cast<const ColumnInt32*>(argument_columns[2].get());
        const auto& part_num_col_data = part_num_col->get_data();

        for (size_t i = 0; i < input_rows_count; ++i) {
            if (part_num_col_data[i] == 0) {
                StringOP::push_null_string(i, res_chars, res_offsets, null_map_data);
                continue;
            }

            auto delimiter = delimiter_col->get_data_at(i);
            auto delimiter_str = delimiter_col->get_data_at(i).to_string();
            auto part_number = part_num_col_data[i];
            auto str = str_col->get_data_at(i);
            if (delimiter.size == 0) {
                StringOP::push_empty_string(i, res_chars, res_offsets);
                continue;
            }

            if (part_number > 0) {
                if (delimiter.size == 1) {
                    // If delimiter is a char, use memchr to split
                    int32_t pre_offset = -1;
                    int32_t offset = -1;
                    int32_t num = 0;
                    while (num < part_number) {
                        pre_offset = offset;
                        size_t n = str.size - offset - 1;
                        const char* pos = reinterpret_cast<const char*>(
                                memchr(str.data + offset + 1, delimiter_str[0], n));
                        if (pos != nullptr) {
                            offset = pos - str.data;
                            num++;
                        } else {
                            offset = str.size;
                            num = (num == 0) ? 0 : num + 1;
                            break;
                        }
                    }

                    if (num == part_number) {
                        StringOP::push_value_string(
                                std::string_view {
                                        reinterpret_cast<const char*>(str.data + pre_offset + 1),
                                        (size_t)offset - pre_offset - 1},
                                i, res_chars, res_offsets);
                    } else {
                        StringOP::push_null_string(i, res_chars, res_offsets, null_map_data);
                    }
                } else {
                    // If delimiter is a string, use memmem to split
                    int32_t pre_offset = -delimiter.size;
                    int32_t offset = -delimiter.size;
                    int32_t num = 0;
                    while (num < part_number) {
                        pre_offset = offset;
                        size_t n = str.size - offset - delimiter.size;
                        char* pos =
                                reinterpret_cast<char*>(memmem(str.data + offset + delimiter.size,
                                                               n, delimiter.data, delimiter.size));
                        if (pos != nullptr) {
                            offset = pos - str.data;
                            num++;
                        } else {
                            offset = str.size;
                            num = (num == 0) ? 0 : num + 1;
                            break;
                        }
                    }

                    if (num == part_number) {
                        StringOP::push_value_string(
                                std::string_view {reinterpret_cast<const char*>(
                                                          str.data + pre_offset + delimiter.size),
                                                  (size_t)offset - pre_offset - delimiter.size},
                                i, res_chars, res_offsets);
                    } else {
                        StringOP::push_null_string(i, res_chars, res_offsets, null_map_data);
                    }
                }
            } else {
                part_number = -part_number;
                auto str_str = str.to_string();
                int32_t offset = str.size;
                int32_t pre_offset = offset;
                int32_t num = 0;
                auto substr = str_str;
                while (num <= part_number && offset >= 0) {
                    offset = (int)substr.rfind(delimiter, offset);
                    if (offset != -1) {
                        if (++num == part_number) {
                            break;
                        }
                        pre_offset = offset;
                        offset = offset - 1;
                        substr = str_str.substr(0, pre_offset);
                    } else {
                        break;
                    }
                }
                num = (offset == -1 && num != 0) ? num + 1 : num;

                if (num == part_number) {
                    if (offset == -1) {
                        StringOP::push_value_string(
                                std::string_view {reinterpret_cast<const char*>(str.data),
                                                  (size_t)pre_offset},
                                i, res_chars, res_offsets);
                    } else {
                        StringOP::push_value_string(
                                std::string_view {str_str.substr(
                                        offset + delimiter.size,
                                        (size_t)pre_offset - offset - delimiter.size)},
                                i, res_chars, res_offsets);
                    }
                } else {
                    StringOP::push_null_string(i, res_chars, res_offsets, null_map_data);
                }
            }
        }

        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res), std::move(null_map));
        return Status::OK();
    }
};

class FunctionSubstringIndex : public IFunction {
public:
    static constexpr auto name = "substring_index";
    static FunctionPtr create() { return std::make_shared<FunctionSubstringIndex>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 3);

        // Create a zero column to simply implement
        auto res = ColumnString::create();

        auto& res_offsets = res->get_offsets();
        auto& res_chars = res->get_chars();
        res_offsets.resize(input_rows_count);
        ColumnPtr content_column;
        bool content_const = false;
        std::tie(content_column, content_const) =
                unpack_if_const(block.get_by_position(arguments[0]).column);

        const auto* str_col = assert_cast<const ColumnString*>(content_column.get());

        // Handle both constant and non-constant delimiter parameters
        ColumnPtr delimiter_column_ptr;
        bool delimiter_const = false;
        std::tie(delimiter_column_ptr, delimiter_const) =
                unpack_if_const(block.get_by_position(arguments[1]).column);
        const auto* delimiter_col = assert_cast<const ColumnString*>(delimiter_column_ptr.get());

        ColumnPtr part_num_column_ptr;
        bool part_num_const = false;
        std::tie(part_num_column_ptr, part_num_const) =
                unpack_if_const(block.get_by_position(arguments[2]).column);
        const ColumnInt32* part_num_col =
                assert_cast<const ColumnInt32*>(part_num_column_ptr.get());

        // For constant multi-character delimiters, create StringRef and StringSearch only once
        std::optional<StringRef> const_delimiter_ref;
        std::optional<StringSearch> const_search;
        if (delimiter_const && delimiter_col->get_data_at(0).size > 1) {
            const_delimiter_ref.emplace(delimiter_col->get_data_at(0));
            const_search.emplace(&const_delimiter_ref.value());
        }

        for (size_t i = 0; i < input_rows_count; ++i) {
            auto str = str_col->get_data_at(i);
            auto delimiter = delimiter_col->get_data_at(delimiter_const ? 0 : i);
            int32_t delimiter_size = delimiter.size;

            auto part_number = part_num_col->get_element(part_num_const ? 0 : i);

            if (part_number == 0 || delimiter_size == 0) {
                StringOP::push_empty_string(i, res_chars, res_offsets);
                continue;
            }

            if (part_number > 0) {
                if (delimiter_size == 1) {
                    int32_t offset = -1;
                    int32_t num = 0;
                    while (num < part_number) {
                        size_t n = str.size - offset - 1;
                        const char* pos = reinterpret_cast<const char*>(
                                memchr(str.data + offset + 1, delimiter.data[0], n));
                        if (pos != nullptr) {
                            offset = pos - str.data;
                            num++;
                        } else {
                            offset = str.size;
                            num = (num == 0) ? 0 : num + 1;
                            break;
                        }
                    }

                    if (num == part_number) {
                        StringOP::push_value_string(
                                std::string_view {reinterpret_cast<const char*>(str.data),
                                                  (size_t)offset},
                                i, res_chars, res_offsets);
                    } else {
                        StringOP::push_value_string(std::string_view(str.data, str.size), i,
                                                    res_chars, res_offsets);
                    }
                } else {
                    // For multi-character delimiters
                    // Use pre-created StringRef and StringSearch for constant delimiters
                    StringRef delimiter_ref = const_delimiter_ref ? const_delimiter_ref.value()
                                                                  : StringRef(delimiter);
                    const StringSearch* search_ptr = const_search ? &const_search.value() : nullptr;
                    StringSearch local_search(&delimiter_ref);
                    if (!search_ptr) {
                        search_ptr = &local_search;
                    }

                    int32_t offset = -delimiter_size;
                    int32_t num = 0;
                    while (num < part_number) {
                        size_t n = str.size - offset - delimiter_size;
                        // search first match delimter_ref index from src string among str_offset to end
                        const char* pos = search_ptr->search(str.data + offset + delimiter_size, n);
                        if (pos < str.data + str.size) {
                            offset = pos - str.data;
                            num++;
                        } else {
                            offset = str.size;
                            num = (num == 0) ? 0 : num + 1;
                            break;
                        }
                    }

                    if (num == part_number) {
                        StringOP::push_value_string(
                                std::string_view {reinterpret_cast<const char*>(str.data),
                                                  (size_t)offset},
                                i, res_chars, res_offsets);
                    } else {
                        StringOP::push_value_string(std::string_view(str.data, str.size), i,
                                                    res_chars, res_offsets);
                    }
                }
            } else {
                int neg_part_number = -part_number;
                auto str_str = str.to_string();
                int32_t offset = str.size;
                int32_t pre_offset = offset;
                int32_t num = 0;
                auto substr = str_str;

                // Use pre-created StringRef for constant delimiters
                StringRef delimiter_str =
                        const_delimiter_ref
                                ? const_delimiter_ref.value()
                                : StringRef(reinterpret_cast<const char*>(delimiter.data),
                                            delimiter.size);

                while (num <= neg_part_number && offset >= 0) {
                    offset = (int)substr.rfind(delimiter_str, offset);
                    if (offset != -1) {
                        if (++num == neg_part_number) {
                            break;
                        }
                        pre_offset = offset;
                        offset = offset - 1;
                        substr = str_str.substr(0, pre_offset);
                    } else {
                        break;
                    }
                }
                num = (offset == -1 && num != 0) ? num + 1 : num;

                if (num == neg_part_number) {
                    if (offset == -1) {
                        StringOP::push_value_string(std::string_view(str.data, str.size), i,
                                                    res_chars, res_offsets);
                    } else {
                        StringOP::push_value_string(
                                std::string_view {str.data + offset + delimiter_size,
                                                  str.size - offset - delimiter_size},
                                i, res_chars, res_offsets);
                    }
                } else {
                    StringOP::push_value_string(std::string_view(str.data, str.size), i, res_chars,
                                                res_offsets);
                }
            }
        }

        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }
};

class FunctionSplitByString : public IFunction {
public:
    static constexpr auto name = "split_by_string";

    static FunctionPtr create() { return std::make_shared<FunctionSplitByString>(); }
    using NullMapType = PaddedPODArray<UInt8>;

    String get_name() const override { return name; }

    bool is_variadic() const override { return false; }

    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        DCHECK(is_string_type(arguments[0]->get_primitive_type()))
                << "first argument for function: " << name << " should be string"
                << " and arguments[0] is " << arguments[0]->get_name();
        DCHECK(is_string_type(arguments[1]->get_primitive_type()))
                << "second argument for function: " << name << " should be string"
                << " and arguments[1] is " << arguments[1]->get_name();
        return std::make_shared<DataTypeArray>(make_nullable(arguments[0]));
    }

    Status execute_impl(FunctionContext* /*context*/, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t /*input_rows_count*/) const override {
        DCHECK_EQ(arguments.size(), 2);

        const auto& [src_column, left_const] =
                unpack_if_const(block.get_by_position(arguments[0]).column);
        const auto& [right_column, right_const] =
                unpack_if_const(block.get_by_position(arguments[1]).column);

        DataTypePtr right_column_type = block.get_by_position(arguments[1]).type;
        DataTypePtr src_column_type = block.get_by_position(arguments[0]).type;
        auto dest_column_ptr = ColumnArray::create(make_nullable(src_column_type)->create_column(),
                                                   ColumnArray::ColumnOffsets::create());

        IColumn* dest_nested_column = &dest_column_ptr->get_data();
        auto& dest_offsets = dest_column_ptr->get_offsets();
        DCHECK(dest_nested_column != nullptr);
        dest_nested_column->reserve(0);
        dest_offsets.reserve(0);

        NullMapType* dest_nested_null_map = nullptr;
        auto* dest_nullable_col = reinterpret_cast<ColumnNullable*>(dest_nested_column);
        dest_nested_column = dest_nullable_col->get_nested_column_ptr().get();
        dest_nested_null_map = &dest_nullable_col->get_null_map_column().get_data();

        const auto* col_left = check_and_get_column<ColumnString>(src_column.get());
        if (!col_left) {
            return Status::InternalError("Left operator of function {} can not be {}", get_name(),
                                         src_column_type->get_name());
        }

        const auto* col_right = check_and_get_column<ColumnString>(right_column.get());
        if (!col_right) {
            return Status::InternalError("Right operator of function {} can not be {}", get_name(),
                                         right_column_type->get_name());
        }

        // split_by_string(ColumnString, "xxx")
        if (right_const) {
            _execute_constant_delimiter(*col_left, col_right->get_data_at(0), *dest_nested_column,
                                        dest_offsets, dest_nested_null_map);
        } else if (left_const) {
            // split_by_string("xxx", ColumnString)
            _execute_constant_src_string(col_left->get_data_at(0), *col_right, *dest_nested_column,
                                         dest_offsets, dest_nested_null_map);
        } else {
            // split_by_string(ColumnString, ColumnString)
            _execute_vector(*col_left, *col_right, *dest_nested_column, dest_offsets,
                            dest_nested_null_map);
        }

        block.replace_by_position(result, std::move(dest_column_ptr));

        return Status::OK();
    }

private:
    void _execute_constant_delimiter(const ColumnString& src_column_string,
                                     const StringRef& delimiter_ref, IColumn& dest_nested_column,
                                     ColumnArray::Offsets64& dest_offsets,
                                     NullMapType* dest_nested_null_map) const {
        auto& dest_column_string = reinterpret_cast<ColumnString&>(dest_nested_column);
        ColumnString::Chars& column_string_chars = dest_column_string.get_chars();
        ColumnString::Offsets& column_string_offsets = dest_column_string.get_offsets();
        column_string_chars.reserve(0);

        ColumnArray::Offset64 string_pos = 0;
        ColumnArray::Offset64 dest_pos = 0;
        ColumnArray::Offset64 src_offsets_size = src_column_string.get_offsets().size();

        StringSearch search(&delimiter_ref);

        for (size_t i = 0; i < src_offsets_size; i++) {
            const StringRef str_ref = src_column_string.get_data_at(i);

            if (str_ref.size == 0) {
                dest_offsets.push_back(dest_pos);
                continue;
            }
            if (delimiter_ref.size == 0) {
                split_empty_delimiter(str_ref, column_string_chars, column_string_offsets,
                                      dest_nested_null_map, string_pos, dest_pos);
            } else {
                for (size_t str_pos = 0; str_pos <= str_ref.size;) {
                    const size_t str_offset = str_pos;
                    const size_t old_size = column_string_chars.size();
                    // search first match delimter_ref index from src string among str_offset to end
                    const char* result_start =
                            search.search(str_ref.data + str_offset, str_ref.size - str_offset);
                    // compute split part size
                    const size_t split_part_size = result_start - str_ref.data - str_offset;
                    // save dist string split part
                    if (split_part_size > 0) {
                        const size_t new_size = old_size + split_part_size;
                        column_string_chars.resize(new_size);
                        memcpy_small_allow_read_write_overflow15(
                                column_string_chars.data() + old_size, str_ref.data + str_offset,
                                split_part_size);
                        // add dist string offset
                        string_pos += split_part_size;
                    }
                    column_string_offsets.push_back(string_pos);
                    // not null
                    (*dest_nested_null_map).push_back(false);
                    // array offset + 1
                    dest_pos++;
                    // add src string str_pos to next search start
                    str_pos += split_part_size + delimiter_ref.size;
                }
            }
            dest_offsets.push_back(dest_pos);
        }
    }

    void _execute_vector(const ColumnString& src_column_string,
                         const ColumnString& delimiter_column, IColumn& dest_nested_column,
                         ColumnArray::Offsets64& dest_offsets,
                         NullMapType* dest_nested_null_map) const {
        auto& dest_column_string = reinterpret_cast<ColumnString&>(dest_nested_column);
        ColumnString::Chars& column_string_chars = dest_column_string.get_chars();
        ColumnString::Offsets& column_string_offsets = dest_column_string.get_offsets();
        column_string_chars.reserve(0);

        ColumnArray::Offset64 string_pos = 0;
        ColumnArray::Offset64 dest_pos = 0;
        ColumnArray::Offset64 src_offsets_size = src_column_string.get_offsets().size();

        for (size_t i = 0; i < src_offsets_size; i++) {
            const StringRef delimiter_ref = delimiter_column.get_data_at(i);
            const StringRef str_ref = src_column_string.get_data_at(i);

            if (str_ref.size == 0) {
                dest_offsets.push_back(dest_pos);
                continue;
            }
            if (delimiter_ref.size == 0) {
                split_empty_delimiter(str_ref, column_string_chars, column_string_offsets,
                                      dest_nested_null_map, string_pos, dest_pos);
            } else {
                for (size_t str_pos = 0; str_pos <= str_ref.size;) {
                    const size_t str_offset = str_pos;
                    const size_t old_size = column_string_chars.size();
                    const size_t split_part_size = split_str(str_pos, str_ref, delimiter_ref);
                    str_pos += delimiter_ref.size;
                    const size_t new_size = old_size + split_part_size;
                    column_string_chars.resize(new_size);
                    if (split_part_size > 0) {
                        memcpy_small_allow_read_write_overflow15(
                                column_string_chars.data() + old_size, str_ref.data + str_offset,
                                split_part_size);
                    }
                    (*dest_nested_null_map).push_back(false);
                    string_pos += split_part_size;
                    dest_pos++;
                    column_string_offsets.push_back(string_pos);
                }
            }
            dest_offsets.push_back(dest_pos);
        }
    }

    void _execute_constant_src_string(const StringRef& str_ref, const ColumnString& delimiter_col,
                                      IColumn& dest_nested_column,
                                      ColumnArray::Offsets64& dest_offsets,
                                      NullMapType* dest_nested_null_map) const {
        auto& dest_column_string = reinterpret_cast<ColumnString&>(dest_nested_column);
        ColumnString::Chars& column_string_chars = dest_column_string.get_chars();
        ColumnString::Offsets& column_string_offsets = dest_column_string.get_offsets();
        column_string_chars.reserve(0);

        ColumnArray::Offset64 string_pos = 0;
        ColumnArray::Offset64 dest_pos = 0;
        const ColumnArray::Offset64 delimiter_offsets_size = delimiter_col.get_offsets().size();

        for (size_t i = 0; i < delimiter_offsets_size; ++i) {
            const StringRef delimiter_ref = delimiter_col.get_data_at(i);

            if (delimiter_ref.size == 0) {
                split_empty_delimiter(str_ref, column_string_chars, column_string_offsets,
                                      dest_nested_null_map, string_pos, dest_pos);
            } else {
                for (size_t str_pos = 0; str_pos <= str_ref.size;) {
                    const size_t str_offset = str_pos;
                    const size_t old_size = column_string_chars.size();
                    const size_t split_part_size = split_str(str_pos, str_ref, delimiter_ref);
                    str_pos += delimiter_ref.size;
                    const size_t new_size = old_size + split_part_size;
                    column_string_chars.resize(new_size);
                    if (split_part_size > 0) {
                        memcpy_small_allow_read_write_overflow15(
                                column_string_chars.data() + old_size, str_ref.data + str_offset,
                                split_part_size);
                    }
                    (*dest_nested_null_map).push_back(false);
                    string_pos += split_part_size;
                    dest_pos++;
                    column_string_offsets.push_back(string_pos);
                }
            }
            dest_offsets.push_back(dest_pos);
        }
    }

    size_t split_str(size_t& pos, const StringRef str_ref, StringRef delimiter_ref) const {
        size_t old_size = pos;
        size_t str_size = str_ref.size;
        while (pos < str_size && memcmp_small_allow_overflow15((const uint8_t*)str_ref.data + pos,
                                                               (const uint8_t*)delimiter_ref.data,
                                                               delimiter_ref.size)) {
            pos++;
        }
        return pos - old_size;
    }

    void split_empty_delimiter(const StringRef& str_ref, ColumnString::Chars& column_string_chars,
                               ColumnString::Offsets& column_string_offsets,
                               NullMapType* dest_nested_null_map, ColumnArray::Offset64& string_pos,
                               ColumnArray::Offset64& dest_pos) const {
        const size_t old_size = column_string_chars.size();
        const size_t new_size = old_size + str_ref.size;
        column_string_chars.resize(new_size);
        memcpy(column_string_chars.data() + old_size, str_ref.data, str_ref.size);
        if (simd::VStringFunctions::is_ascii(str_ref)) {
            const auto size = str_ref.size;

            dest_nested_null_map->resize_fill(dest_nested_null_map->size() + size, false);

            const auto old_size = column_string_offsets.size();
            const auto new_size = old_size + size;
            column_string_offsets.resize(new_size);
            std::iota(column_string_offsets.data() + old_size,
                      column_string_offsets.data() + new_size, string_pos + 1);

            string_pos += size;
            dest_pos += size;
            // The above code is equivalent to the code in the following comment.
            // for (size_t i = 0; i < str_ref.size; i++) {
            //     string_pos++;
            //     column_string_offsets.push_back(string_pos);
            //     (*dest_nested_null_map).push_back(false);
            //     dest_pos++;
            // }
        } else {
            for (size_t i = 0, utf8_char_len = 0; i < str_ref.size; i += utf8_char_len) {
                utf8_char_len = UTF8_BYTE_LENGTH[(unsigned char)str_ref.data[i]];

                string_pos += utf8_char_len;
                column_string_offsets.push_back(string_pos);

                (*dest_nested_null_map).push_back(false);
                dest_pos++;
            }
        }
    }
};

class FunctionCountSubString : public IFunction {
public:
    static constexpr auto name = "count_substrings";

    static FunctionPtr create() { return std::make_shared<FunctionCountSubString>(); }
    using NullMapType = PaddedPODArray<UInt8>;

    String get_name() const override { return name; }

    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        DCHECK(is_string_type(arguments[0]->get_primitive_type()))
                << "first argument for function: " << name << " should be string"
                << " and arguments[0] is " << arguments[0]->get_name();
        DCHECK(is_string_type(arguments[1]->get_primitive_type()))
                << "second argument for function: " << name << " should be string"
                << " and arguments[1] is " << arguments[1]->get_name();
        return std::make_shared<DataTypeInt32>();
    }

    Status execute_impl(FunctionContext* /*context*/, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 2);
        const auto& [src_column, left_const] =
                unpack_if_const(block.get_by_position(arguments[0]).column);
        const auto& [right_column, right_const] =
                unpack_if_const(block.get_by_position(arguments[1]).column);

        const auto* col_left = check_and_get_column<ColumnString>(src_column.get());
        if (!col_left) {
            return Status::InternalError("Left operator of function {} can not be {}", get_name(),
                                         block.get_by_position(arguments[0]).type->get_name());
        }

        const auto* col_right = check_and_get_column<ColumnString>(right_column.get());
        if (!col_right) {
            return Status::InternalError("Right operator of function {} can not be {}", get_name(),
                                         block.get_by_position(arguments[1]).type->get_name());
        }

        auto dest_column_ptr = ColumnInt32::create(input_rows_count, 0);
        // count_substring(ColumnString, "xxx")
        if (right_const) {
            _execute_constant_pattern(*col_left, col_right->get_data_at(0),
                                      dest_column_ptr->get_data(), input_rows_count);
        } else if (left_const) {
            // count_substring("xxx", ColumnString)
            _execute_constant_src_string(col_left->get_data_at(0), *col_right,
                                         dest_column_ptr->get_data(), input_rows_count);
        } else {
            // count_substring(ColumnString, ColumnString)
            _execute_vector(*col_left, *col_right, dest_column_ptr->get_data(), input_rows_count);
        }

        block.replace_by_position(result, std::move(dest_column_ptr));
        return Status::OK();
    }

private:
    void _execute_constant_pattern(const ColumnString& src_column_string,
                                   const StringRef& pattern_ref,
                                   ColumnInt32::Container& dest_column_data,
                                   size_t input_rows_count) const {
        for (size_t i = 0; i < input_rows_count; i++) {
            const StringRef str_ref = src_column_string.get_data_at(i);
            dest_column_data[i] = find_str_count(str_ref, pattern_ref);
        }
    }

    void _execute_vector(const ColumnString& src_column_string, const ColumnString& pattern_column,
                         ColumnInt32::Container& dest_column_data, size_t input_rows_count) const {
        for (size_t i = 0; i < input_rows_count; i++) {
            const StringRef pattern_ref = pattern_column.get_data_at(i);
            const StringRef str_ref = src_column_string.get_data_at(i);
            dest_column_data[i] = find_str_count(str_ref, pattern_ref);
        }
    }

    void _execute_constant_src_string(const StringRef& str_ref, const ColumnString& pattern_col,
                                      ColumnInt32::Container& dest_column_data,
                                      size_t input_rows_count) const {
        for (size_t i = 0; i < input_rows_count; ++i) {
            const StringRef pattern_ref = pattern_col.get_data_at(i);
            dest_column_data[i] = find_str_count(str_ref, pattern_ref);
        }
    }

    size_t find_pos(size_t pos, const StringRef str_ref, const StringRef pattern_ref) const {
        size_t old_size = pos;
        size_t str_size = str_ref.size;
        while (pos < str_size &&
               memcmp_small_allow_overflow15((const uint8_t*)str_ref.data + pos,
                                             (const uint8_t*)pattern_ref.data, pattern_ref.size)) {
            pos++;
        }
        return pos - old_size;
    }

    int find_str_count(const StringRef str_ref, StringRef pattern_ref) const {
        int count = 0;
        if (str_ref.size == 0 || pattern_ref.size == 0) {
            return 0;
        } else {
            for (size_t str_pos = 0; str_pos <= str_ref.size;) {
                const size_t res_pos = find_pos(str_pos, str_ref, pattern_ref);
                if (res_pos == (str_ref.size - str_pos)) {
                    break; // not find
                }
                count++;
                str_pos = str_pos + res_pos + pattern_ref.size;
            }
        }
        return count;
    }
};

struct SM3Sum {
    static constexpr auto name = "sm3sum";
    using ObjectData = SM3Digest;
};

struct MD5Sum {
    static constexpr auto name = "md5sum";
    using ObjectData = Md5Digest;
};

template <typename Impl>
class FunctionStringDigestOneArg : public IFunction {
public:
    static constexpr auto name = Impl::name;
    static FunctionPtr create() { return std::make_shared<FunctionStringDigestOneArg>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_GE(arguments.size(), 1);

        int argument_size = arguments.size();
        std::vector<ColumnPtr> argument_columns(argument_size);

        std::vector<const ColumnString::Offsets*> offsets_list(argument_size);
        std::vector<const ColumnString::Chars*> chars_list(argument_size);

        for (int i = 0; i < argument_size; ++i) {
            argument_columns[i] =
                    block.get_by_position(arguments[i]).column->convert_to_full_column_if_const();
            if (const auto* col_str = assert_cast<const ColumnString*>(argument_columns[i].get())) {
                offsets_list[i] = &col_str->get_offsets();
                chars_list[i] = &col_str->get_chars();
            } else {
                return Status::RuntimeError("Illegal column {} of argument of function {}",
                                            block.get_by_position(arguments[0]).column->get_name(),
                                            get_name());
            }
        }

        auto res = ColumnString::create();
        auto& res_data = res->get_chars();
        auto& res_offset = res->get_offsets();

        res_offset.resize(input_rows_count);
        for (size_t i = 0; i < input_rows_count; ++i) {
            using ObjectData = typename Impl::ObjectData;
            ObjectData digest;
            for (size_t j = 0; j < offsets_list.size(); ++j) {
                const auto& current_offsets = *offsets_list[j];
                const auto& current_chars = *chars_list[j];

                int size = current_offsets[i] - current_offsets[i - 1];
                if (size < 1) {
                    continue;
                }
                digest.update(&current_chars[current_offsets[i - 1]], size);
            }
            digest.digest();

            StringOP::push_value_string(std::string_view(digest.hex().c_str(), digest.hex().size()),
                                        i, res_data, res_offset);
        }

        block.replace_by_position(result, std::move(res));
        return Status::OK();
    }
};

class FunctionStringDigestSHA1 : public IFunction {
public:
    static constexpr auto name = "sha1";
    static FunctionPtr create() { return std::make_shared<FunctionStringDigestSHA1>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 1; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 1);

        ColumnPtr str_col = block.get_by_position(arguments[0]).column;
        auto& data = assert_cast<const ColumnString*>(str_col.get())->get_chars();
        auto& offset = assert_cast<const ColumnString*>(str_col.get())->get_offsets();

        auto res_col = ColumnString::create();
        auto& res_data = res_col->get_chars();
        auto& res_offset = res_col->get_offsets();
        res_offset.resize(input_rows_count);

        SHA1Digest digest;
        for (size_t i = 0; i < input_rows_count; ++i) {
            int size = offset[i] - offset[i - 1];
            digest.reset(&data[offset[i - 1]], size);
            std::string_view ans = digest.digest();

            StringOP::push_value_string(ans, i, res_data, res_offset);
        }

        block.replace_by_position(result, std::move(res_col));
        return Status::OK();
    }
};

class FunctionStringDigestSHA2 : public IFunction {
public:
    static constexpr auto name = "sha2";
    static FunctionPtr create() { return std::make_shared<FunctionStringDigestSHA2>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 2; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK(!is_column_const(*block.get_by_position(arguments[0]).column));

        ColumnPtr str_col = block.get_by_position(arguments[0]).column;
        auto& data = assert_cast<const ColumnString*>(str_col.get())->get_chars();
        auto& offset = assert_cast<const ColumnString*>(str_col.get())->get_offsets();

        [[maybe_unused]] const auto& [right_column, right_const] =
                unpack_if_const(block.get_by_position(arguments[1]).column);
        auto digest_length = assert_cast<const ColumnInt32*>(right_column.get())->get_data()[0];

        auto res_col = ColumnString::create();
        auto& res_data = res_col->get_chars();
        auto& res_offset = res_col->get_offsets();
        res_offset.resize(input_rows_count);

        if (digest_length == 224) {
            execute_base<SHA224Digest>(data, offset, input_rows_count, res_data, res_offset);
        } else if (digest_length == 256) {
            execute_base<SHA256Digest>(data, offset, input_rows_count, res_data, res_offset);
        } else if (digest_length == 384) {
            execute_base<SHA384Digest>(data, offset, input_rows_count, res_data, res_offset);
        } else if (digest_length == 512) {
            execute_base<SHA512Digest>(data, offset, input_rows_count, res_data, res_offset);
        } else {
            return Status::InvalidArgument(
                    "sha2's digest length only support 224/256/384/512 but meet {}", digest_length);
        }

        block.replace_by_position(result, std::move(res_col));
        return Status::OK();
    }

private:
    template <typename T>
    void execute_base(const ColumnString::Chars& data, const ColumnString::Offsets& offset,
                      int input_rows_count, ColumnString::Chars& res_data,
                      ColumnString::Offsets& res_offset) const {
        T digest;
        for (size_t i = 0; i < input_rows_count; ++i) {
            int size = offset[i] - offset[i - 1];
            digest.reset(&data[offset[i - 1]], size);
            std::string_view ans = digest.digest();

            StringOP::push_value_string(ans, i, res_data, res_offset);
        }
    }
};

class FunctionExtractURLParameter : public IFunction {
public:
    static constexpr auto name = "extract_url_parameter";
    static FunctionPtr create() { return std::make_shared<FunctionExtractURLParameter>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto col_url =
                block.get_by_position(arguments[0]).column->convert_to_full_column_if_const();
        auto col_parameter =
                block.get_by_position(arguments[1]).column->convert_to_full_column_if_const();
        auto url_col = assert_cast<const ColumnString*>(col_url.get());
        auto parameter_col = assert_cast<const ColumnString*>(col_parameter.get());

        ColumnString::MutablePtr col_res = ColumnString::create();

        for (int i = 0; i < input_rows_count; ++i) {
            auto source = url_col->get_data_at(i);
            auto param = parameter_col->get_data_at(i);
            auto res = extract_url(source, param);

            col_res->insert_data(res.data, res.size);
        }

        block.replace_by_position(result, std::move(col_res));
        return Status::OK();
    }

private:
    StringRef extract_url(StringRef url, StringRef parameter) const {
        if (url.size == 0 || parameter.size == 0) {
            return StringRef("", 0);
        }
        return UrlParser::extract_url(url, parameter);
    }
};

class FunctionStringParseUrl : public IFunction {
public:
    static constexpr auto name = "parse_url";
    static FunctionPtr create() { return std::make_shared<FunctionStringParseUrl>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        auto& null_map_data = null_map->get_data();
        DCHECK_GE(3, arguments.size());
        auto res = ColumnString::create();
        auto& res_offsets = res->get_offsets();
        auto& res_chars = res->get_chars();
        res_offsets.resize(input_rows_count);

        size_t argument_size = arguments.size();
        const bool has_key = argument_size == 3;

        std::vector<ColumnPtr> argument_columns(argument_size);
        std::vector<UInt8> col_const(argument_size);
        for (size_t i = 0; i < argument_size; ++i) {
            std::tie(argument_columns[i], col_const[i]) =
                    unpack_if_const(block.get_by_position(arguments[i]).column);
        }

        const auto* url_col = assert_cast<const ColumnString*>(argument_columns[0].get());
        const auto* part_col = assert_cast<const ColumnString*>(argument_columns[1].get());
        const bool part_const = col_const[1];
        std::vector<UrlParser::UrlPart> url_parts;
        const int part_nums = part_const ? 1 : input_rows_count;

        url_parts.resize(part_nums);
        for (int i = 0; i < part_nums; i++) {
            StringRef part = part_col->get_data_at(i);
            UrlParser::UrlPart url_part = UrlParser::get_url_part(part);
            if (url_part == UrlParser::INVALID) {
                return Status::RuntimeError("Invalid URL part: {}\n{}",
                                            std::string(part.data, part.size),
                                            "(Valid URL parts are 'PROTOCOL', 'HOST', "
                                            "'PATH', 'REF', 'AUTHORITY', "
                                            "'FILE', 'USERINFO', 'PORT' and 'QUERY')");
            }
            url_parts[i] = url_part;
        }

        if (has_key) {
            const bool url_const = col_const[0];
            const bool key_const = col_const[2];
            const auto* key_col = assert_cast<const ColumnString*>(argument_columns[2].get());
            RETURN_IF_ERROR(std::visit(
                    [&](auto url_const, auto part_const, auto key_const) {
                        return vector_parse_key<url_const, part_const, key_const>(
                                url_col, url_parts, key_col, input_rows_count, null_map_data,
                                res_chars, res_offsets);
                    },
                    vectorized::make_bool_variant(url_const),
                    vectorized::make_bool_variant(part_const),
                    vectorized::make_bool_variant(key_const)));
        } else {
            const bool url_const = col_const[0];
            RETURN_IF_ERROR(std::visit(
                    [&](auto url_const, auto part_const) {
                        return vector_parse<url_const, part_const>(url_col, url_parts,
                                                                   input_rows_count, null_map_data,
                                                                   res_chars, res_offsets);
                    },
                    vectorized::make_bool_variant(url_const),
                    vectorized::make_bool_variant(part_const)));
        }
        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res), std::move(null_map));
        return Status::OK();
    }
    template <bool url_const, bool part_const>
    static Status vector_parse(const ColumnString* url_col,
                               std::vector<UrlParser::UrlPart>& url_parts, const int size,
                               ColumnUInt8::Container& null_map_data,
                               ColumnString::Chars& res_chars, ColumnString::Offsets& res_offsets) {
        for (size_t i = 0; i < size; ++i) {
            UrlParser::UrlPart& url_part = url_parts[index_check_const<part_const>(i)];
            StringRef url_val = url_col->get_data_at(index_check_const<url_const>(i));
            StringRef parse_res;
            if (UrlParser::parse_url(url_val, url_part, &parse_res)) {
                if (parse_res.empty()) [[unlikely]] {
                    StringOP::push_empty_string(i, res_chars, res_offsets);
                    continue;
                }
                StringOP::push_value_string(std::string_view(parse_res.data, parse_res.size), i,
                                            res_chars, res_offsets);
            } else {
                StringOP::push_null_string(i, res_chars, res_offsets, null_map_data);
            }
        }
        return Status::OK();
    }
    template <bool url_const, bool part_const, bool key_const>
    static Status vector_parse_key(const ColumnString* url_col,
                                   std::vector<UrlParser::UrlPart>& url_parts,
                                   const ColumnString* key_col, const int size,
                                   ColumnUInt8::Container& null_map_data,
                                   ColumnString::Chars& res_chars,
                                   ColumnString::Offsets& res_offsets) {
        for (size_t i = 0; i < size; ++i) {
            UrlParser::UrlPart& url_part = url_parts[index_check_const<part_const>(i)];
            StringRef url_val = url_col->get_data_at(index_check_const<url_const>(i));
            StringRef url_key = key_col->get_data_at(index_check_const<key_const>(i));
            StringRef parse_res;
            if (UrlParser::parse_url_key(url_val, url_part, url_key, &parse_res)) {
                StringOP::push_value_string(std::string_view(parse_res.data, parse_res.size), i,
                                            res_chars, res_offsets);
            } else {
                StringOP::push_null_string(i, res_chars, res_offsets, null_map_data);
                continue;
            }
        }
        return Status::OK();
    }
};

class FunctionUrlDecode : public IFunction {
public:
    static constexpr auto name = "url_decode";
    static FunctionPtr create() { return std::make_shared<FunctionUrlDecode>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 1; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto res = ColumnString::create();
        res->get_offsets().reserve(input_rows_count);

        const auto* url_col =
                assert_cast<const ColumnString*>(block.get_by_position(arguments[0]).column.get());

        std::string decoded_url;
        for (size_t i = 0; i < input_rows_count; ++i) {
            auto url = url_col->get_data_at(i);
            if (!url_decode(url.to_string(), &decoded_url)) {
                return Status::InternalError("Decode url failed");
            }
            res->insert_data(decoded_url.data(), decoded_url.size());
            decoded_url.clear();
        }

        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }
};

class FunctionUrlEncode : public IFunction {
public:
    static constexpr auto name = "url_encode";
    static FunctionPtr create() { return std::make_shared<FunctionUrlEncode>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 1; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto res = ColumnString::create();
        res->get_offsets().reserve(input_rows_count);

        const auto* url_col =
                assert_cast<const ColumnString*>(block.get_by_position(arguments[0]).column.get());

        std::string encoded_url;
        for (size_t i = 0; i < input_rows_count; ++i) {
            auto url = url_col->get_data_at(i);
            url_encode(url.to_string_view(), &encoded_url);
            res->insert_data(encoded_url.data(), encoded_url.size());
            encoded_url.clear();
        }

        block.get_by_position(result).column = std::move(res);
        return Status::OK();
    }
};

class FunctionRandomBytes : public IFunction {
public:
    static constexpr auto name = "random_bytes";
    static FunctionPtr create() { return std::make_shared<FunctionRandomBytes>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 1; }
    bool is_variadic() const override { return false; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    bool use_default_implementation_for_constants() const final { return false; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto res = ColumnString::create();
        auto& res_offsets = res->get_offsets();
        auto& res_chars = res->get_chars();
        res_offsets.resize(input_rows_count);

        auto [arg_col, arg_const] = unpack_if_const(block.get_by_position(arguments[0]).column);
        const auto* length_col = assert_cast<const ColumnInt32*>(arg_col.get());

        if (arg_const) {
            res_chars.reserve(input_rows_count * (length_col->get_element(0) + 2));
        }

        std::vector<uint8_t, Allocator_<uint8_t>> random_bytes;
        std::random_device rd;
        std::mt19937 gen(rd());

        std::uniform_int_distribution<unsigned short> distribution(0, 255);
        for (size_t i = 0; i < input_rows_count; ++i) {
            size_t index = index_check_const(i, arg_const);
            if (length_col->get_element(index) < 0) [[unlikely]] {
                return Status::InvalidArgument("argument {} of function {} at row {} was invalid.",
                                               length_col->get_element(index), name, index);
            }
            random_bytes.resize(length_col->get_element(index));

            for (auto& byte : random_bytes) {
                byte = distribution(gen) & 0xFF;
            }

            std::basic_ostringstream<char, std::char_traits<char>, Allocator_<char>> oss;
            for (const auto& byte : random_bytes) {
                oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte);
            }

            StringOP::push_value_string("0x" + oss.str(), i, res_chars, res_offsets);
            random_bytes.clear();
        }

        block.get_by_position(result).column = std::move(res);

        return Status::OK();
    }
};

template <typename Impl>
class FunctionMoneyFormat : public IFunction {
public:
    static constexpr auto name = "money_format";
    static FunctionPtr create() { return std::make_shared<FunctionMoneyFormat>(); }
    String get_name() const override { return name; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        if (arguments.size() != 1) {
            throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                   "Function {} requires exactly 1 argument", name);
        }

        return std::make_shared<DataTypeString>();
    }
    DataTypes get_variadic_argument_types_impl() const override {
        return Impl::get_variadic_argument_types();
    }
    size_t get_number_of_arguments() const override { return 1; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        auto res_column = ColumnString::create();
        ColumnPtr argument_column = block.get_by_position(arguments[0]).column;

        auto result_column = assert_cast<ColumnString*>(res_column.get());

        Impl::execute(context, result_column, argument_column, input_rows_count);

        block.replace_by_position(result, std::move(res_column));
        return Status::OK();
    }
};

// ----------------------------------------------------------------------
// SimpleItoaWithCommas()
//    Description: converts an integer to a string.
//    Puts commas every 3 spaces.
//    Faster than printf("%d")?
//
//    Return value: string
// ----------------------------------------------------------------------
template <typename T>
char* SimpleItoaWithCommas(T i, char* buffer, int32_t buffer_size) {
    char* p = buffer + buffer_size;
    // Need to use unsigned T instead of T to correctly handle
    std::make_unsigned_t<T> n = i;
    if (i < 0) {
        n = 0 - n;
    }
    *--p = '0' + n % 10; // this case deals with the number "0"
    n /= 10;
    while (n) {
        *--p = '0' + n % 10;
        n /= 10;
        if (n == 0) {
            break;
        }

        *--p = '0' + n % 10;
        n /= 10;
        if (n == 0) {
            break;
        }

        *--p = ',';
        *--p = '0' + n % 10;
        n /= 10;
        // For this unrolling, we check if n == 0 in the main while loop
    }
    if (i < 0) {
        *--p = '-';
    }
    return p;
}

namespace MoneyFormat {

constexpr size_t MAX_FORMAT_LEN_DEC32() {
    // Decimal(9, 0)
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 9 + (9 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_DEC64() {
    // Decimal(18, 0)
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 18 + (18 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_DEC128V2() {
    // DecimalV2 has at most 27 digits
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 27 + (27 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_DEC128V3() {
    // Decimal(38, 0)
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 39 + (39 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_INT64() {
    // INT_MIN = -9223372036854775807
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 20 + (20 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_INT128() {
    // INT128_MIN = -170141183460469231731687303715884105728
    return 2 * (1 + 39 + (39 / 3) + 3);
}

template <typename T, size_t N>
StringRef do_money_format(FunctionContext* context, UInt32 scale, T int_value, T frac_value) {
    static_assert(std::is_integral<T>::value);
    const bool is_negative = int_value < 0 || frac_value < 0;

    // do round to frac_part
    // magic number 2: since we need to round frac_part to 2 digits
    if (scale > 2) {
        DCHECK(scale <= 38);
        // do rounding, so we need to reserve 3 digits.
        auto multiplier = common::exp10_i128(std::abs(static_cast<int>(scale - 3)));
        // do devide first to avoid overflow
        // after round frac_value will be positive by design.
        frac_value = std::abs(static_cast<int>(frac_value / multiplier)) + 5;
        frac_value /= 10;
    } else if (scale < 2) {
        DCHECK(frac_value < 100);
        // since scale <= 2, overflow is impossiable
        frac_value = frac_value * common::exp10_i32(2 - scale);
    }

    if (frac_value == 100) {
        if (is_negative) {
            int_value -= 1;
        } else {
            int_value += 1;
        }
        frac_value = 0;
    }

    bool append_sign_manually = false;
    if (is_negative && int_value == 0) {
        // when int_value is 0, result of SimpleItoaWithCommas will contains just zero
        // for Decimal like -0.1234, this will leads to problem, because negative sign is discarded.
        // this is why we introduce argument append_sing_manually.
        append_sign_manually = true;
    }

    char local[N];
    char* p = SimpleItoaWithCommas<T>(int_value, local, sizeof(local));
    const Int32 integer_str_len = N - (p - local);
    const Int32 frac_str_len = 2;
    const Int32 whole_decimal_str_len =
            (append_sign_manually ? 1 : 0) + integer_str_len + 1 + frac_str_len;

    StringRef result = context->create_temp_string_val(whole_decimal_str_len);
    char* result_data = const_cast<char*>(result.data);

    if (append_sign_manually) {
        memset(result_data, '-', 1);
    }

    memcpy(result_data + (append_sign_manually ? 1 : 0), p, integer_str_len);
    *(result_data + whole_decimal_str_len - 3) = '.';
    *(result_data + whole_decimal_str_len - 2) = '0' + std::abs(static_cast<int>(frac_value / 10));
    *(result_data + whole_decimal_str_len - 1) = '0' + std::abs(static_cast<int>(frac_value % 10));
    return result;
};

// Note string value must be valid decimal string which contains two digits after the decimal point
static StringRef do_money_format(FunctionContext* context, const std::string& value) {
    bool is_positive = (value[0] != '-');
    int32_t result_len = value.size() + (value.size() - (is_positive ? 4 : 5)) / 3;
    StringRef result = context->create_temp_string_val(result_len);
    char* result_data = const_cast<char*>(result.data);
    if (!is_positive) {
        *result_data = '-';
    }
    for (int i = value.size() - 4, j = result_len - 4; i >= 0; i = i - 3) {
        *(result_data + j) = *(value.data() + i);
        if (i - 1 < 0) {
            break;
        }
        *(result_data + j - 1) = *(value.data() + i - 1);
        if (i - 2 < 0) {
            break;
        }
        *(result_data + j - 2) = *(value.data() + i - 2);
        if (j - 3 > 1 || (j - 3 == 1 && is_positive)) {
            *(result_data + j - 3) = ',';
            j -= 4;
        } else {
            j -= 3;
        }
    }
    memcpy(result_data + result_len - 3, value.data() + value.size() - 3, 3);
    return result;
};

} // namespace MoneyFormat

namespace FormatRound {

constexpr size_t MAX_FORMAT_LEN_DEC32() {
    // Decimal(9, 0)
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 9 + (9 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_DEC64() {
    // Decimal(18, 0)
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 18 + (18 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_DEC128V2() {
    // DecimalV2 has at most 27 digits
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 27 + (27 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_DEC128V3() {
    // Decimal(38, 0)
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 39 + (39 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_INT64() {
    // INT_MIN = -9223372036854775807
    // Double the size to avoid some unexpected bug.
    return 2 * (1 + 20 + (20 / 3) + 3);
}

constexpr size_t MAX_FORMAT_LEN_INT128() {
    // INT128_MIN = -170141183460469231731687303715884105728
    return 2 * (1 + 39 + (39 / 3) + 3);
}

template <typename T, size_t N>
StringRef do_format_round(FunctionContext* context, UInt32 scale, T int_value, T frac_value,
                          Int32 decimal_places) {
    static_assert(std::is_integral<T>::value);
    const bool is_negative = int_value < 0 || frac_value < 0;

    // do round to frac_part based on decimal_places
    if (scale > decimal_places && decimal_places > 0) {
        DCHECK(scale <= 38);
        // do rounding, so we need to reserve decimal_places + 1 digits
        auto multiplier =
                common::exp10_i128(std::abs(static_cast<int>(scale - (decimal_places + 1))));
        // do divide first to avoid overflow
        // after round frac_value will be positive by design
        frac_value = std::abs(static_cast<int>(frac_value / multiplier)) + 5;
        frac_value /= 10;
    } else if (scale < decimal_places && decimal_places > 0) {
        // since scale <= decimal_places, overflow is impossible
        frac_value = frac_value * common::exp10_i32(decimal_places - scale);
    }

    // Calculate power of 10 for decimal_places
    T decimal_power = common::exp10_i32(decimal_places);
    if (frac_value == decimal_power) {
        if (is_negative) {
            int_value -= 1;
        } else {
            int_value += 1;
        }
        frac_value = 0;
    }

    bool append_sign_manually = false;
    if (is_negative && int_value == 0) {
        append_sign_manually = true;
    }

    char local[N];
    char* p = SimpleItoaWithCommas<T>(int_value, local, sizeof(local));
    const Int32 integer_str_len = N - (p - local);
    const Int32 frac_str_len = decimal_places;
    const Int32 whole_decimal_str_len = (append_sign_manually ? 1 : 0) + integer_str_len +
                                        (decimal_places > 0 ? 1 : 0) + frac_str_len;

    StringRef result = context->create_temp_string_val(whole_decimal_str_len);
    char* result_data = const_cast<char*>(result.data);

    if (append_sign_manually) {
        memset(result_data, '-', 1);
    }

    memcpy(result_data + (append_sign_manually ? 1 : 0), p, integer_str_len);
    if (decimal_places > 0) {
        *(result_data + whole_decimal_str_len - (frac_str_len + 1)) = '.';
    }

    // Convert fractional part to string with proper padding
    T remaining_frac = std::abs(static_cast<int>(frac_value));
    for (int i = 0; i <= decimal_places - 1; ++i) {
        *(result_data + whole_decimal_str_len - 1 - i) = '0' + (remaining_frac % 10);
        remaining_frac /= 10;
    }
    return result;
}

// Note string value must be valid decimal string which contains two digits after the decimal point
static StringRef do_format_round(FunctionContext* context, const std::string& value,
                                 Int32 decimal_places) {
    bool is_positive = (value[0] != '-');
    int32_t result_len =
            value.size() +
            (value.size() - (is_positive ? (decimal_places + 2) : (decimal_places + 3))) / 3;
    StringRef result = context->create_temp_string_val(result_len);
    char* result_data = const_cast<char*>(result.data);
    if (!is_positive) {
        *result_data = '-';
    }
    for (int i = value.size() - (decimal_places + 2), j = result_len - (decimal_places + 2); i >= 0;
         i = i - 3) {
        *(result_data + j) = *(value.data() + i);
        if (i - 1 < 0) {
            break;
        }
        *(result_data + j - 1) = *(value.data() + i - 1);
        if (i - 2 < 0) {
            break;
        }
        *(result_data + j - 2) = *(value.data() + i - 2);
        if (j - 3 > 1 || (j - 3 == 1 && is_positive)) {
            *(result_data + j - 3) = ',';
            j -= 4;
        } else {
            j -= 3;
        }
    }
    memcpy(result_data + result_len - (decimal_places + 1),
           value.data() + value.size() - (decimal_places + 1), (decimal_places + 1));
    return result;
};

} // namespace FormatRound

struct MoneyFormatDoubleImpl {
    static DataTypes get_variadic_argument_types() { return {std::make_shared<DataTypeFloat64>()}; }

    static void execute(FunctionContext* context, ColumnString* result_column,
                        const ColumnPtr col_ptr, size_t input_rows_count) {
        const auto* data_column = assert_cast<const ColumnFloat64*>(col_ptr.get());
        // when scale is above 38, we will go here
        for (size_t i = 0; i < input_rows_count; i++) {
            // round to 2 decimal places
            double value =
                    MathFunctions::my_double_round(data_column->get_element(i), 2, false, false);
            StringRef str = MoneyFormat::do_money_format(context, fmt::format("{:.2f}", value));
            result_column->insert_data(str.data, str.size);
        }
    }
};

struct MoneyFormatInt64Impl {
    static DataTypes get_variadic_argument_types() { return {std::make_shared<DataTypeInt64>()}; }

    static void execute(FunctionContext* context, ColumnString* result_column,
                        const ColumnPtr col_ptr, size_t input_rows_count) {
        const auto* data_column = assert_cast<const ColumnInt64*>(col_ptr.get());
        for (size_t i = 0; i < input_rows_count; i++) {
            Int64 value = data_column->get_element(i);
            StringRef str =
                    MoneyFormat::do_money_format<Int64, MoneyFormat::MAX_FORMAT_LEN_INT64()>(
                            context, 0, value, 0);
            result_column->insert_data(str.data, str.size);
        }
    }
};

struct MoneyFormatInt128Impl {
    static DataTypes get_variadic_argument_types() { return {std::make_shared<DataTypeInt128>()}; }

    static void execute(FunctionContext* context, ColumnString* result_column,
                        const ColumnPtr col_ptr, size_t input_rows_count) {
        const auto* data_column = assert_cast<const ColumnInt128*>(col_ptr.get());
        // SELECT money_format(170141183460469231731687303715884105728/*INT128_MAX + 1*/) will
        // get "170,141,183,460,469,231,731,687,303,715,884,105,727.00" in doris,
        // see https://github.com/apache/doris/blob/788abf2d7c3c7c2d57487a9608e889e7662d5fb2/be/src/vec/data_types/data_type_number_base.cpp#L124
        for (size_t i = 0; i < input_rows_count; i++) {
            Int128 value = data_column->get_element(i);
            StringRef str =
                    MoneyFormat::do_money_format<Int128, MoneyFormat::MAX_FORMAT_LEN_INT128()>(
                            context, 0, value, 0);
            result_column->insert_data(str.data, str.size);
        }
    }
};

template <PrimitiveType Type>
struct MoneyFormatDecimalImpl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename PrimitiveTypeTraits<Type>::DataType>()};
    }

    static void execute(FunctionContext* context, ColumnString* result_column, ColumnPtr col_ptr,
                        size_t input_rows_count) {
        if (auto* decimalv2_column = check_and_get_column<ColumnDecimal128V2>(*col_ptr)) {
            for (size_t i = 0; i < input_rows_count; i++) {
                const Decimal128V2& dec128 = decimalv2_column->get_element(i);
                DecimalV2Value value = DecimalV2Value(dec128.value);
                // unified_frac_value has 3 digits
                auto unified_frac_value = value.frac_value() / 1000000;
                StringRef str =
                        MoneyFormat::do_money_format<Int128,
                                                     MoneyFormat::MAX_FORMAT_LEN_DEC128V2()>(
                                context, 3, value.int_value(), unified_frac_value);

                result_column->insert_data(str.data, str.size);
            }
        } else if (auto* decimal32_column = check_and_get_column<ColumnDecimal32>(*col_ptr)) {
            const UInt32 scale = decimal32_column->get_scale();
            for (size_t i = 0; i < input_rows_count; i++) {
                const Decimal32& frac_part = decimal32_column->get_fractional_part(i);
                const Decimal32& whole_part = decimal32_column->get_whole_part(i);
                StringRef str =
                        MoneyFormat::do_money_format<Int64, MoneyFormat::MAX_FORMAT_LEN_DEC32()>(
                                context, scale, static_cast<Int64>(whole_part.value),
                                static_cast<Int64>(frac_part.value));

                result_column->insert_data(str.data, str.size);
            }
        } else if (auto* decimal64_column = check_and_get_column<ColumnDecimal64>(*col_ptr)) {
            const UInt32 scale = decimal64_column->get_scale();
            for (size_t i = 0; i < input_rows_count; i++) {
                const Decimal64& frac_part = decimal64_column->get_fractional_part(i);
                const Decimal64& whole_part = decimal64_column->get_whole_part(i);

                StringRef str =
                        MoneyFormat::do_money_format<Int64, MoneyFormat::MAX_FORMAT_LEN_DEC64()>(
                                context, scale, whole_part.value, frac_part.value);

                result_column->insert_data(str.data, str.size);
            }
        } else if (auto* decimal128_column = check_and_get_column<ColumnDecimal128V3>(*col_ptr)) {
            const UInt32 scale = decimal128_column->get_scale();
            for (size_t i = 0; i < input_rows_count; i++) {
                const Decimal128V3& frac_part = decimal128_column->get_fractional_part(i);
                const Decimal128V3& whole_part = decimal128_column->get_whole_part(i);

                StringRef str =
                        MoneyFormat::do_money_format<Int128,
                                                     MoneyFormat::MAX_FORMAT_LEN_DEC128V3()>(
                                context, scale, whole_part.value, frac_part.value);

                result_column->insert_data(str.data, str.size);
            }
        } else {
            throw doris::Exception(ErrorCode::INVALID_ARGUMENT,
                                   "Not supported input argument type {}", col_ptr->get_name());
        }
        // TODO: decimal256
        /* else if (auto* decimal256_column =
                           check_and_get_column<ColumnDecimal<Decimal256>>(*col_ptr)) {
            const UInt32 scale = decimal256_column->get_scale();
            const auto multiplier =
                    scale > 2 ? common::exp10_i32(scale - 2) : common::exp10_i32(2 - scale);
            for (size_t i = 0; i < input_rows_count; i++) {
                Decimal256 frac_part = decimal256_column->get_fractional_part(i);
                if (scale > 2) {
                    int delta = ((frac_part % multiplier) << 1) > multiplier;
                    frac_part = Decimal256(frac_part / multiplier + delta);
                } else if (scale < 2) {
                    frac_part = Decimal256(frac_part * multiplier);
                }

                StringRef str = MoneyFormat::do_money_format<int64_t, 26>(
                        context, decimal256_column->get_whole_part(i), frac_part);

                result_column->insert_data(str.data, str.size);
            }
        }*/
    }
};

struct FormatRoundDoubleImpl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeFloat64>(), std::make_shared<vectorized::DataTypeInt32>()};
    }

    static Status execute(FunctionContext* context, ColumnString* result_column,
                          const ColumnPtr col_ptr, ColumnPtr decimal_places_col_ptr,
                          size_t input_rows_count) {
        const auto& arg_column_data_2 =
                assert_cast<const ColumnInt32*>(decimal_places_col_ptr.get())->get_data();
        const auto* data_column = assert_cast<const ColumnFloat64*>(col_ptr.get());
        // when scale is above 38, we will go here
        for (size_t i = 0; i < input_rows_count; i++) {
            int32_t decimal_places = arg_column_data_2[i];
            if (decimal_places < 0) {
                return Status::InvalidArgument(
                        "The second argument is {}, it can not be less than 0.", decimal_places);
            }
            // round to `decimal_places` decimal places
            double value = MathFunctions::my_double_round(data_column->get_element(i),
                                                          decimal_places, false, false);
            StringRef str = FormatRound::do_format_round(
                    context, fmt::format("{:.{}f}", value, decimal_places), decimal_places);
            result_column->insert_data(str.data, str.size);
        }
        return Status::OK();
    }
};

struct FormatRoundInt64Impl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeInt64>(), std::make_shared<vectorized::DataTypeInt32>()};
    }

    static Status execute(FunctionContext* context, ColumnString* result_column,
                          const ColumnPtr col_ptr, ColumnPtr decimal_places_col_ptr,
                          size_t input_rows_count) {
        const auto* data_column = assert_cast<const ColumnInt64*>(col_ptr.get());
        const auto& arg_column_data_2 =
                assert_cast<const ColumnInt32*>(decimal_places_col_ptr.get())->get_data();
        for (size_t i = 0; i < input_rows_count; i++) {
            int32_t decimal_places = arg_column_data_2[i];
            if (decimal_places < 0) {
                return Status::InvalidArgument(
                        "The second argument is {}, it can not be less than 0.", decimal_places);
            }
            Int64 value = data_column->get_element(i);
            StringRef str =
                    FormatRound::do_format_round<Int64, FormatRound::MAX_FORMAT_LEN_INT64()>(
                            context, 0, value, 0, decimal_places);
            result_column->insert_data(str.data, str.size);
        }
        return Status::OK();
    }
};

struct FormatRoundInt128Impl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeInt128>(), std::make_shared<vectorized::DataTypeInt32>()};
    }

    static Status execute(FunctionContext* context, ColumnString* result_column,
                          const ColumnPtr col_ptr, ColumnPtr decimal_places_col_ptr,
                          size_t input_rows_count) {
        const auto* data_column = assert_cast<const ColumnInt128*>(col_ptr.get());
        const auto& arg_column_data_2 =
                assert_cast<const ColumnInt32*>(decimal_places_col_ptr.get())->get_data();
        // SELECT money_format(170141183460469231731687303715884105728/*INT128_MAX + 1*/) will
        // get "170,141,183,460,469,231,731,687,303,715,884,105,727.00" in doris,
        // see https://github.com/apache/doris/blob/788abf2d7c3c7c2d57487a9608e889e7662d5fb2/be/src/vec/data_types/data_type_number_base.cpp#L124
        for (size_t i = 0; i < input_rows_count; i++) {
            int32_t decimal_places = arg_column_data_2[i];
            if (decimal_places < 0) {
                return Status::InvalidArgument(
                        "The second argument is {}, it can not be less than 0.", decimal_places);
            }
            Int128 value = data_column->get_element(i);
            StringRef str =
                    FormatRound::do_format_round<Int128, FormatRound::MAX_FORMAT_LEN_INT128()>(
                            context, 0, value, 0, decimal_places);
            result_column->insert_data(str.data, str.size);
        }
        return Status::OK();
    }
};

template <PrimitiveType Type>
struct FormatRoundDecimalImpl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<typename PrimitiveTypeTraits<Type>::DataType>(),
                std::make_shared<vectorized::DataTypeInt32>()};
    }

    static Status execute(FunctionContext* context, ColumnString* result_column, ColumnPtr col_ptr,
                          ColumnPtr decimal_places_col_ptr, size_t input_rows_count) {
        const auto& arg_column_data_2 =
                assert_cast<const ColumnInt32*>(decimal_places_col_ptr.get())->get_data();
        if (auto* decimalv2_column = check_and_get_column<ColumnDecimal128V2>(*col_ptr)) {
            for (size_t i = 0; i < input_rows_count; i++) {
                int32_t decimal_places = arg_column_data_2[i];
                if (decimal_places < 0) {
                    return Status::InvalidArgument(
                            "The second argument is {}, it can not be less than 0.",
                            decimal_places);
                }
                const Decimal128V2& dec128 = decimalv2_column->get_element(i);
                DecimalV2Value value = DecimalV2Value(dec128.value);
                // unified_frac_value has 3 digits
                auto unified_frac_value = value.frac_value() / 1000000;
                StringRef str =
                        FormatRound::do_format_round<Int128,
                                                     FormatRound::MAX_FORMAT_LEN_DEC128V2()>(
                                context, 3, value.int_value(), unified_frac_value, decimal_places);

                result_column->insert_data(str.data, str.size);
            }
        } else if (auto* decimal32_column = check_and_get_column<ColumnDecimal32>(*col_ptr)) {
            const UInt32 scale = decimal32_column->get_scale();
            for (size_t i = 0; i < input_rows_count; i++) {
                int32_t decimal_places = arg_column_data_2[i];
                if (decimal_places < 0) {
                    return Status::InvalidArgument(
                            "The second argument is {}, it can not be less than 0.",
                            decimal_places);
                }
                const Decimal32& frac_part = decimal32_column->get_fractional_part(i);
                const Decimal32& whole_part = decimal32_column->get_whole_part(i);
                StringRef str =
                        FormatRound::do_format_round<Int64, FormatRound::MAX_FORMAT_LEN_DEC32()>(
                                context, scale, static_cast<Int64>(whole_part.value),
                                static_cast<Int64>(frac_part.value), decimal_places);

                result_column->insert_data(str.data, str.size);
            }
        } else if (auto* decimal64_column = check_and_get_column<ColumnDecimal64>(*col_ptr)) {
            const UInt32 scale = decimal64_column->get_scale();
            for (size_t i = 0; i < input_rows_count; i++) {
                int32_t decimal_places = arg_column_data_2[i];
                if (decimal_places < 0) {
                    return Status::InvalidArgument(
                            "The second argument is {}, it can not be less than 0.",
                            decimal_places);
                }
                const Decimal64& frac_part = decimal64_column->get_fractional_part(i);
                const Decimal64& whole_part = decimal64_column->get_whole_part(i);

                StringRef str =
                        FormatRound::do_format_round<Int64, FormatRound::MAX_FORMAT_LEN_DEC64()>(
                                context, scale, whole_part.value, frac_part.value, decimal_places);

                result_column->insert_data(str.data, str.size);
            }
        } else if (auto* decimal128_column = check_and_get_column<ColumnDecimal128V3>(*col_ptr)) {
            const UInt32 scale = decimal128_column->get_scale();
            for (size_t i = 0; i < input_rows_count; i++) {
                int32_t decimal_places = arg_column_data_2[i];
                if (decimal_places < 0) {
                    return Status::InvalidArgument(
                            "The second argument is {}, it can not be less than 0.",
                            decimal_places);
                }
                const Decimal128V3& frac_part = decimal128_column->get_fractional_part(i);
                const Decimal128V3& whole_part = decimal128_column->get_whole_part(i);

                StringRef str =
                        FormatRound::do_format_round<Int128,
                                                     FormatRound::MAX_FORMAT_LEN_DEC128V3()>(
                                context, scale, whole_part.value, frac_part.value, decimal_places);

                result_column->insert_data(str.data, str.size);
            }
        } else {
            return Status::InternalError("Not supported input argument type {}",
                                         col_ptr->get_name());
        }
        return Status::OK();
    }
};

class FunctionStringLocatePos : public IFunction {
public:
    static constexpr auto name = "locate";
    static FunctionPtr create() { return std::make_shared<FunctionStringLocatePos>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeInt32>();
    }

    DataTypes get_variadic_argument_types_impl() const override {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>(),
                std::make_shared<DataTypeInt32>()};
    }

    bool is_variadic() const override { return true; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 3);
        bool col_const[3];
        ColumnPtr argument_columns[3];
        for (int i = 0; i < 3; ++i) {
            col_const[i] = is_column_const(*block.get_by_position(arguments[i]).column);
        }
        argument_columns[2] = col_const[2] ? static_cast<const ColumnConst&>(
                                                     *block.get_by_position(arguments[2]).column)
                                                     .convert_to_full_column()
                                           : block.get_by_position(arguments[2]).column;
        default_preprocess_parameter_columns(argument_columns, col_const, {0, 1}, block, arguments);

        auto col_left = assert_cast<const ColumnString*>(argument_columns[0].get());
        auto col_right = assert_cast<const ColumnString*>(argument_columns[1].get());
        auto col_pos = assert_cast<const ColumnInt32*>(argument_columns[2].get());

        ColumnInt32::MutablePtr col_res = ColumnInt32::create();
        auto& vec_res = col_res->get_data();
        vec_res.resize(block.rows());

        if (col_const[0] && col_const[1]) {
            scalar_search<true>(col_left->get_data_at(0), col_right, col_pos->get_data(), vec_res);
        } else if (col_const[0] && !col_const[1]) {
            scalar_search<false>(col_left->get_data_at(0), col_right, col_pos->get_data(), vec_res);
        } else if (!col_const[0] && col_const[1]) {
            vector_search<true>(col_left, col_right, col_pos->get_data(), vec_res);
        } else {
            vector_search<false>(col_left, col_right, col_pos->get_data(), vec_res);
        }
        block.replace_by_position(result, std::move(col_res));
        return Status::OK();
    }

private:
    template <bool Const>
    void scalar_search(const StringRef& ldata, const ColumnString* col_right,
                       const PaddedPODArray<Int32>& posdata, PaddedPODArray<Int32>& res) const {
        const ColumnString::Chars& rdata = col_right->get_chars();
        const ColumnString::Offsets& roffsets = col_right->get_offsets();

        auto size = posdata.size();
        res.resize(size);
        StringRef substr(ldata.data, ldata.size);
        std::shared_ptr<StringSearch> search_ptr(new StringSearch(&substr));

        for (int i = 0; i < size; ++i) {
            if constexpr (!Const) {
                const char* r_raw_str = reinterpret_cast<const char*>(&rdata[roffsets[i - 1]]);
                int r_str_size = roffsets[i] - roffsets[i - 1];

                StringRef str(r_raw_str, r_str_size);
                res[i] = locate_pos(substr, str, search_ptr, posdata[i]);
            } else {
                res[i] = locate_pos(substr, col_right->get_data_at(0), search_ptr, posdata[i]);
            }
        }
    }

    template <bool Const>
    void vector_search(const ColumnString* col_left, const ColumnString* col_right,
                       const PaddedPODArray<Int32>& posdata, PaddedPODArray<Int32>& res) const {
        const ColumnString::Chars& rdata = col_right->get_chars();
        const ColumnString::Offsets& roffsets = col_right->get_offsets();

        const ColumnString::Chars& ldata = col_left->get_chars();
        const ColumnString::Offsets& loffsets = col_left->get_offsets();

        auto size = posdata.size();
        res.resize(size);
        std::shared_ptr<StringSearch> search_ptr;
        for (int i = 0; i < size; ++i) {
            const char* l_raw_str = reinterpret_cast<const char*>(&ldata[loffsets[i - 1]]);
            int l_str_size = loffsets[i] - loffsets[i - 1];

            StringRef substr(l_raw_str, l_str_size);
            if constexpr (!Const) {
                const char* r_raw_str = reinterpret_cast<const char*>(&rdata[roffsets[i - 1]]);
                int r_str_size = roffsets[i] - roffsets[i - 1];

                StringRef str(r_raw_str, r_str_size);
                res[i] = locate_pos(substr, str, search_ptr, posdata[i]);
            } else {
                res[i] = locate_pos(substr, col_right->get_data_at(0), search_ptr, posdata[i]);
            }
        }
    }

    int locate_pos(StringRef substr, StringRef str, std::shared_ptr<StringSearch> search_ptr,
                   int start_pos) const {
        if (substr.size == 0) {
            if (start_pos <= 0) {
                return 0;
            } else if (start_pos == 1) {
                return 1;
            } else if (start_pos > str.size) {
                return 0;
            } else {
                return start_pos;
            }
        }
        // Hive returns 0 for *start_pos <= 0,
        // but throws an exception for *start_pos > str->len.
        // Since returning 0 seems to be Hive's error condition, return 0.
        std::vector<size_t> index;
        size_t char_len = simd::VStringFunctions::get_char_len(str.data, str.size, index);
        if (start_pos <= 0 || start_pos > str.size || start_pos > char_len) {
            return 0;
        }
        if (!search_ptr) {
            search_ptr.reset(new StringSearch(&substr));
        }
        // Input start_pos starts from 1.
        StringRef adjusted_str(str.data + index[start_pos - 1], str.size - index[start_pos - 1]);
        int32_t match_pos = search_ptr->search(&adjusted_str);
        if (match_pos >= 0) {
            // Hive returns the position in the original string starting from 1.
            size_t len = std::min(adjusted_str.size, (size_t)match_pos);
            return start_pos + simd::VStringFunctions::get_char_len(adjusted_str.data, len);
        } else {
            return 0;
        }
    }
};

struct ReplaceImpl {
    static constexpr auto name = "replace";
};

struct ReplaceEmptyImpl {
    static constexpr auto name = "replace_empty";
};

template <typename Impl, bool empty>
class FunctionReplace : public IFunction {
public:
    static constexpr auto name = Impl::name;
    static FunctionPtr create() { return std::make_shared<FunctionReplace<Impl, empty>>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    DataTypes get_variadic_argument_types_impl() const override {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>(),
                std::make_shared<DataTypeString>()};
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        // We need a local variable to hold a reference to the converted column.
        // So that the converted column will not be released before we use it.
        ColumnPtr col[3];
        bool col_const[3];
        for (size_t i = 0; i < 3; ++i) {
            std::tie(col[i], col_const[i]) =
                    unpack_if_const(block.get_by_position(arguments[i]).column);
        }

        const auto* col_origin_str = assert_cast<const ColumnString*>(col[0].get());
        const auto* col_old_str = assert_cast<const ColumnString*>(col[1].get());
        const auto* col_new_str = assert_cast<const ColumnString*>(col[2].get());

        ColumnString::MutablePtr col_res = ColumnString::create();

        std::visit(
                [&](auto origin_str_const, auto old_str_const, auto new_str_const) {
                    for (int i = 0; i < input_rows_count; ++i) {
                        StringRef origin_str =
                                col_origin_str->get_data_at(index_check_const<origin_str_const>(i));
                        StringRef old_str =
                                col_old_str->get_data_at(index_check_const<old_str_const>(i));
                        StringRef new_str =
                                col_new_str->get_data_at(index_check_const<new_str_const>(i));

                        std::string result =
                                replace(origin_str.to_string(), old_str.to_string_view(),
                                        new_str.to_string_view());

                        col_res->insert_data(result.data(), result.length());
                    }
                },
                vectorized::make_bool_variant(col_const[0]),
                vectorized::make_bool_variant(col_const[1]),
                vectorized::make_bool_variant(col_const[2]));

        block.replace_by_position(result, std::move(col_res));
        return Status::OK();
    }

private:
    std::string replace(std::string str, std::string_view old_str, std::string_view new_str) const {
        if (old_str.empty()) {
            if constexpr (empty) {
                return str;
            } else {
                // Different from "Replace" only when the search string is empty.
                // it will insert `new_str` in front of every character and at the end of the old str.
                if (new_str.empty()) {
                    return str;
                }
                if (simd::VStringFunctions::is_ascii({str.data(), str.size()})) {
                    std::string result;
                    ColumnString::check_chars_length(
                            str.length() * (new_str.length() + 1) + new_str.length(), 0);
                    result.reserve(str.length() * (new_str.length() + 1) + new_str.length());
                    for (char c : str) {
                        result += new_str;
                        result += c;
                    }
                    result += new_str;
                    return result;
                } else {
                    std::string result;
                    result.reserve(str.length() * (new_str.length() + 1) + new_str.length());
                    for (size_t i = 0, utf8_char_len = 0; i < str.size(); i += utf8_char_len) {
                        utf8_char_len = UTF8_BYTE_LENGTH[(unsigned char)str[i]];
                        result += new_str;
                        result.append(&str[i], utf8_char_len);
                    }
                    result += new_str;
                    ColumnString::check_chars_length(result.size(), 0);
                    return result;
                }
            }
        } else {
            std::string::size_type pos = 0;
            std::string::size_type oldLen = old_str.size();
            std::string::size_type newLen = new_str.size();
            while ((pos = str.find(old_str, pos)) != std::string::npos) {
                str.replace(pos, oldLen, new_str);
                pos += newLen;
            }
            return str;
        }
    }
};

struct ReverseImpl {
    static Status vector(const ColumnString::Chars& data, const ColumnString::Offsets& offsets,
                         ColumnString::Chars& res_data, ColumnString::Offsets& res_offsets) {
        auto rows_count = offsets.size();
        res_offsets.resize(rows_count);
        res_data.reserve(data.size());
        for (ssize_t i = 0; i < rows_count; ++i) {
            auto src_str = reinterpret_cast<const char*>(&data[offsets[i - 1]]);
            int64_t src_len = offsets[i] - offsets[i - 1];
            std::string dst;
            dst.resize(src_len);
            simd::VStringFunctions::reverse(StringRef((uint8_t*)src_str, src_len),
                                            StringRef((uint8_t*)dst.data(), src_len));
            StringOP::push_value_string(std::string_view(dst.data(), src_len), i, res_data,
                                        res_offsets);
        }
        return Status::OK();
    }
};

template <typename Impl>
class FunctionSubReplace : public IFunction {
public:
    static constexpr auto name = "sub_replace";

    static FunctionPtr create() { return std::make_shared<FunctionSubReplace<Impl>>(); }

    String get_name() const override { return name; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }

    bool is_variadic() const override { return true; }

    DataTypes get_variadic_argument_types_impl() const override {
        return Impl::get_variadic_argument_types();
    }

    size_t get_number_of_arguments() const override {
        return get_variadic_argument_types_impl().size();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        return Impl::execute_impl(context, block, arguments, result, input_rows_count);
    }
};

struct SubReplaceImpl {
    static Status replace_execute(Block& block, const ColumnNumbers& arguments, uint32_t result,
                                  size_t input_rows_count) {
        auto res_column = ColumnString::create();
        auto* result_column = assert_cast<ColumnString*>(res_column.get());
        auto args_null_map = ColumnUInt8::create(input_rows_count, 0);
        ColumnPtr argument_columns[4];
        bool col_const[4];
        for (int i = 0; i < 4; ++i) {
            std::tie(argument_columns[i], col_const[i]) =
                    unpack_if_const(block.get_by_position(arguments[i]).column);
        }
        const auto* data_column = assert_cast<const ColumnString*>(argument_columns[0].get());
        const auto* mask_column = assert_cast<const ColumnString*>(argument_columns[1].get());
        const auto* start_column = assert_cast<const ColumnInt32*>(argument_columns[2].get());
        const auto* length_column = assert_cast<const ColumnInt32*>(argument_columns[3].get());

        std::visit(
                [&](auto origin_str_const, auto new_str_const, auto start_const, auto len_const) {
                    if (data_column->is_ascii()) {
                        vector_ascii<origin_str_const, new_str_const, start_const, len_const>(
                                data_column, mask_column, start_column->get_data(),
                                length_column->get_data(), args_null_map->get_data(), result_column,
                                input_rows_count);
                    } else {
                        vector_utf8<origin_str_const, new_str_const, start_const, len_const>(
                                data_column, mask_column, start_column->get_data(),
                                length_column->get_data(), args_null_map->get_data(), result_column,
                                input_rows_count);
                    }
                },
                vectorized::make_bool_variant(col_const[0]),
                vectorized::make_bool_variant(col_const[1]),
                vectorized::make_bool_variant(col_const[2]),
                vectorized::make_bool_variant(col_const[3]));
        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res_column), std::move(args_null_map));
        return Status::OK();
    }

private:
    template <bool origin_str_const, bool new_str_const, bool start_const, bool len_const>
    static void vector_ascii(const ColumnString* data_column, const ColumnString* mask_column,
                             const PaddedPODArray<Int32>& args_start,
                             const PaddedPODArray<Int32>& args_length, NullMap& args_null_map,
                             ColumnString* result_column, size_t input_rows_count) {
        ColumnString::Chars& res_chars = result_column->get_chars();
        ColumnString::Offsets& res_offsets = result_column->get_offsets();
        for (size_t row = 0; row < input_rows_count; ++row) {
            StringRef origin_str =
                    data_column->get_data_at(index_check_const<origin_str_const>(row));
            StringRef new_str = mask_column->get_data_at(index_check_const<new_str_const>(row));
            const auto start = args_start[index_check_const<start_const>(row)];
            const auto length = args_length[index_check_const<len_const>(row)];
            const size_t origin_str_len = origin_str.size;
            //input is null, start < 0, len < 0, str_size <= start. return NULL
            if (args_null_map[row] || start < 0 || length < 0 || origin_str_len <= start) {
                res_offsets.push_back(res_chars.size());
                args_null_map[row] = 1;
            } else {
                std::string_view replace_str = new_str.to_string_view();
                std::string result = origin_str.to_string();
                result.replace(start, length, replace_str);
                result_column->insert_data(result.data(), result.length());
            }
        }
    }

    template <bool origin_str_const, bool new_str_const, bool start_const, bool len_const>
    static void vector_utf8(const ColumnString* data_column, const ColumnString* mask_column,
                            const PaddedPODArray<Int32>& args_start,
                            const PaddedPODArray<Int32>& args_length, NullMap& args_null_map,
                            ColumnString* result_column, size_t input_rows_count) {
        ColumnString::Chars& res_chars = result_column->get_chars();
        ColumnString::Offsets& res_offsets = result_column->get_offsets();

        for (size_t row = 0; row < input_rows_count; ++row) {
            StringRef origin_str =
                    data_column->get_data_at(index_check_const<origin_str_const>(row));
            StringRef new_str = mask_column->get_data_at(index_check_const<new_str_const>(row));
            const auto start = args_start[index_check_const<start_const>(row)];
            const auto length = args_length[index_check_const<len_const>(row)];
            //input is null, start < 0, len < 0 return NULL
            if (args_null_map[row] || start < 0 || length < 0) {
                res_offsets.push_back(res_chars.size());
                args_null_map[row] = 1;
                continue;
            }

            const auto [start_byte_len, start_char_len] =
                    simd::VStringFunctions::iterate_utf8_with_limit_length(origin_str.begin(),
                                                                           origin_str.end(), start);

            // start >= orgin.size
            DCHECK(start_char_len <= start);
            if (start_byte_len == origin_str.size) {
                res_offsets.push_back(res_chars.size());
                args_null_map[row] = 1;
                continue;
            }

            auto [end_byte_len, end_char_len] =
                    simd::VStringFunctions::iterate_utf8_with_limit_length(
                            origin_str.begin() + start_byte_len, origin_str.end(), length);
            DCHECK(end_char_len <= length);
            std::string_view replace_str = new_str.to_string_view();
            std::string result = origin_str.to_string();
            result.replace(start_byte_len, end_byte_len, replace_str);
            result_column->insert_data(result.data(), result.length());
        }
    }
};

struct SubReplaceThreeImpl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>(),
                std::make_shared<DataTypeInt32>()};
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, uint32_t result,
                               size_t input_rows_count) {
        auto params = ColumnInt32::create(input_rows_count);
        auto& strlen_data = params->get_data();

        auto str_col =
                block.get_by_position(arguments[1]).column->convert_to_full_column_if_const();
        if (const auto* nullable = check_and_get_column<const ColumnNullable>(*str_col)) {
            str_col = nullable->get_nested_column_ptr();
        }
        const auto* str_column = assert_cast<const ColumnString*>(str_col.get());
        // use utf8 len
        for (int i = 0; i < input_rows_count; ++i) {
            StringRef str_ref = str_column->get_data_at(i);
            strlen_data[i] = simd::VStringFunctions::get_char_len(str_ref.data, str_ref.size);
        }

        block.insert({std::move(params), std::make_shared<DataTypeInt32>(), "strlen"});
        ColumnNumbers temp_arguments = {arguments[0], arguments[1], arguments[2],
                                        block.columns() - 1};
        return SubReplaceImpl::replace_execute(block, temp_arguments, result, input_rows_count);
    }
};

struct SubReplaceFourImpl {
    static DataTypes get_variadic_argument_types() {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>(),
                std::make_shared<DataTypeInt32>(), std::make_shared<DataTypeInt32>()};
    }

    static Status execute_impl(FunctionContext* context, Block& block,
                               const ColumnNumbers& arguments, uint32_t result,
                               size_t input_rows_count) {
        return SubReplaceImpl::replace_execute(block, arguments, result, input_rows_count);
    }
};

class FunctionConvertTo : public IFunction {
public:
    static constexpr auto name = "convert_to";

    static FunctionPtr create() { return std::make_shared<FunctionConvertTo>(); }

    String get_name() const override { return name; }

    size_t get_number_of_arguments() const override { return 2; }

    DataTypePtr get_return_type_impl(const DataTypes& /*arguments*/) const override {
        return std::make_shared<DataTypeString>();
    }

    Status open(FunctionContext* context, FunctionContext::FunctionStateScope scope) override {
        if (scope != FunctionContext::THREAD_LOCAL) {
            return Status::OK();
        }
        if (!context->is_col_constant(1)) {
            return Status::InvalidArgument(
                    "character argument to convert function must be constant.");
        }
        const auto& character_data = context->get_constant_col(1)->column_ptr->get_data_at(0);
        if (!iequal(character_data.to_string(), "gbk")) {
            return Status::RuntimeError(
                    "Illegal second argument column of function convert. now only support "
                    "convert to character set of gbk");
        }

        return Status::OK();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        ColumnPtr argument_column =
                block.get_by_position(arguments[0]).column->convert_to_full_column_if_const();
        const ColumnString* str_col = static_cast<const ColumnString*>(argument_column.get());
        const auto& str_offset = str_col->get_offsets();
        const auto& str_chars = str_col->get_chars();
        auto col_res = ColumnString::create();
        auto& res_offset = col_res->get_offsets();
        auto& res_chars = col_res->get_chars();
        res_offset.resize(input_rows_count);
        // max pinyin size is 6 + 1 (first '~') for utf8 chinese word 3
        size_t pinyin_size = (str_chars.size() + 2) / 3 * 7;
        ColumnString::check_chars_length(pinyin_size, 0);
        res_chars.resize(pinyin_size);

        size_t in_len = 0, out_len = 0;
        for (int i = 0; i < input_rows_count; ++i) {
            in_len = str_offset[i] - str_offset[i - 1];
            const char* in = reinterpret_cast<const char*>(&str_chars[str_offset[i - 1]]);
            char* out = reinterpret_cast<char*>(&res_chars[res_offset[i - 1]]);
            _utf8_to_pinyin(in, in_len, out, &out_len);
            res_offset[i] = res_offset[i - 1] + out_len;
        }
        res_chars.resize(res_offset[input_rows_count - 1]);
        block.replace_by_position(result, std::move(col_res));
        return Status::OK();
    }

    void _utf8_to_pinyin(const char* in, size_t in_len, char* out, size_t* out_len) const {
        auto do_memcpy = [](char*& dest, const char*& from, size_t size) {
            memcpy_small_allow_read_write_overflow15(dest, from, size);
            dest += size;
            from += size;
        };
        auto from = in;
        auto dest = out;

        while (from - in < in_len) {
            auto length = get_utf8_byte_length(*from);
            if (length != 3) {
                do_memcpy(dest, from, length);
            } else {
                // convert utf8 to unicode code to get pinyin offset
                if (auto tmp = (((int)(*from & 0x0F)) << 12) | (((int)(*(from + 1) & 0x3F)) << 6) |
                               (*(from + 2) & 0x3F);
                    tmp >= START_UNICODE_OFFSET and tmp < END_UNICODE_OFFSET) {
                    const char* buf = nullptr;
                    if (tmp >= START_UNICODE_OFFSET && tmp < MID_UNICODE_OFFSET) {
                        buf = PINYIN_DICT1 + (tmp - START_UNICODE_OFFSET) * MAX_PINYIN_LEN;
                    } else if (tmp >= MID_UNICODE_OFFSET && tmp < END_UNICODE_OFFSET) {
                        buf = PINYIN_DICT2 + (tmp - MID_UNICODE_OFFSET) * MAX_PINYIN_LEN;
                    }

                    auto end = strchr(buf, ' ');
                    // max len for pinyin is 6
                    int len = MAX_PINYIN_LEN;
                    if (end != nullptr && end - buf < MAX_PINYIN_LEN) {
                        len = end - buf;
                    }
                    // set first char '~' just make sure all english word lower than chinese word
                    *dest = 126;
                    memcpy(dest + 1, buf, len);
                    dest += (len + 1);
                    from += 3;
                } else {
                    do_memcpy(dest, from, 3);
                }
            }
        }

        *out_len = dest - out;
    }
};

// refer to https://dev.mysql.com/doc/refman/8.0/en/string-functions.html#function_char
//      UTF8
// 多	0xe5, 0xa4, 0x9a	0xb6, 0xe0
// 睿	0xe7, 0x9d, 0xbf	0xee, 0xa3
// 丝	0xe4, 0xb8, 0x9d	0xcb, 0xbf 14989469
// MySQL behaviour:
// mysql> select char(0xe4, 0xb8, 0x9d using utf8);
// +-----------------------------------+
// | char(0xe4, 0xb8, 0x9d using utf8) |
// +-----------------------------------+
// | 丝                                |
// +-----------------------------------+
// 1 row in set, 1 warning (0.00 sec)
// mysql> select char(14989469 using utf8);
// +---------------------------+
// | char(14989469 using utf8) |
// +---------------------------+
// | 丝                        |
// +---------------------------+
// 1 row in set, 1 warning (0.00 sec)
// mysql> select char(0xe5, 0xa4, 0x9a, 0xe7, 0x9d, 0xbf, 0xe4, 0xb8, 0x9d, 68, 111, 114, 105, 115 using utf8);
// +---------------------------------------------------------------------------------------------+
// | char(0xe5, 0xa4, 0x9a, 0xe7, 0x9d, 0xbf, 0xe4, 0xb8, 0x9d, 68, 111, 114, 105, 115 using utf8) |
// +---------------------------------------------------------------------------------------------+
// | 多睿丝 Doris                                                                                 |
// +---------------------------------------------------------------------------------------------+
// mysql> select char(68, 111, 114, 0, 105, null, 115 using utf8);
// +--------------------------------------------------+
// | char(68, 111, 114, 0, 105, null, 115 using utf8) |
// +--------------------------------------------------+
// | Dor is                                           |
// +--------------------------------------------------+

// return null:
// mysql>  select char(255 using utf8);
// +----------------------+
// | char(255 using utf8) |
// +----------------------+
// | NULL                 |
// +----------------------+
// 1 row in set, 2 warnings (0.00 sec)
//
// mysql> show warnings;
// +---------+------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
// | Level   | Code | Message                                                                                                                                                                     |
// +---------+------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
// | Warning | 3719 | 'utf8' is currently an alias for the character set UTF8MB3, but will be an alias for UTF8MB4 in a future release. Please consider using UTF8MB4 in order to be unambiguous. |
// | Warning | 1300 | Invalid utf8mb3 character string: 'FF'                                                                                                                                      |
// +---------+------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
// 2 rows in set (0.01 sec)

// max int value:
// mysql> select char(18446744073709551615);
// +--------------------------------------------------------+
// | char(18446744073709551615)                             |
// +--------------------------------------------------------+
// | 0xFFFFFFFF                                             |
// +--------------------------------------------------------+
// 1 row in set (0.00 sec)
//
// mysql> select char(18446744073709551616);
// +--------------------------------------------------------+
// | char(18446744073709551616)                             |
// +--------------------------------------------------------+
// | 0xFFFFFFFF                                             |
// +--------------------------------------------------------+
// 1 row in set, 1 warning (0.00 sec)
//
// mysql> show warnings;
// +---------+------+-----------------------------------------------------------+
// | Level   | Code | Message                                                   |
// +---------+------+-----------------------------------------------------------+
// | Warning | 1292 | Truncated incorrect DECIMAL value: '18446744073709551616' |
// +---------+------+-----------------------------------------------------------+
// 1 row in set (0.00 sec)

// table columns:
// mysql> select * from t;
// +------+------+------+
// | f1   | f2   | f3   |
// +------+------+------+
// |  228 |  184 |  157 |
// |  228 |  184 |    0 |
// |  228 |  184 |   99 |
// |   99 |  228 |  184 |
// +------+------+------+
// 4 rows in set (0.00 sec)
//
// mysql> select char(f1, f2, f3 using utf8) from t;
// +-----------------------------+
// | char(f1, f2, f3 using utf8) |
// +-----------------------------+
// | 丝                          |
// |                             |
// |                             |
// | c                           |
// +-----------------------------+
// 4 rows in set, 4 warnings (0.00 sec)
//
// mysql> show warnings;
// +---------+------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
// | Level   | Code | Message                                                                                                                                                                     |
// +---------+------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
// | Warning | 3719 | 'utf8' is currently an alias for the character set UTF8MB3, but will be an alias for UTF8MB4 in a future release. Please consider using UTF8MB4 in order to be unambiguous. |
// | Warning | 1300 | Invalid utf8mb3 character string: 'E4B800'                                                                                                                                  |
// | Warning | 1300 | Invalid utf8mb3 character string: 'E4B863'                                                                                                                                  |
// | Warning | 1300 | Invalid utf8mb3 character string: 'E4B8'                                                                                                                                    |
// +---------+------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
class FunctionIntToChar : public IFunction {
public:
    static constexpr auto name = "char";
    static FunctionPtr create() { return std::make_shared<FunctionIntToChar>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 0; }
    bool is_variadic() const override { return true; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }
    bool use_default_implementation_for_nulls() const override { return false; }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_GE(arguments.size(), 2);

        int argument_size = arguments.size();
        std::vector<ColumnPtr> str_columns(argument_size - 1);
        std::vector<const ColumnString::Offsets*> offsets_list(argument_size - 1);
        std::vector<const ColumnString::Chars*> chars_list(argument_size - 1);

        // convert each argument columns to column string and then concat the string columns
        for (size_t i = 1; i < argument_size; ++i) {
            if (auto const_column = check_and_get_column<const ColumnConst>(
                        *block.get_by_position(arguments[i]).column)) {
                // ignore null
                if (const_column->only_null()) {
                    str_columns[i - 1] = nullptr;
                } else {
                    auto str_column = ColumnString::create();
                    auto& chars = str_column->get_chars();
                    auto& offsets = str_column->get_offsets();
                    offsets.resize(1);
                    const ColumnInt32* int_column;
                    if (auto* nullable = check_and_get_column<const ColumnNullable>(
                                const_column->get_data_column())) {
                        int_column = assert_cast<const ColumnInt32*>(
                                nullable->get_nested_column_ptr().get());
                    } else {
                        int_column =
                                assert_cast<const ColumnInt32*>(&const_column->get_data_column());
                    }
                    int int_val = int_column->get_int(0);
                    integer_to_char_(0, &int_val, chars, offsets);
                    str_columns[i - 1] =
                            ColumnConst::create(std::move(str_column), input_rows_count);
                }
                offsets_list[i - 1] = nullptr;
                chars_list[i - 1] = nullptr;
            } else {
                auto str_column = ColumnString::create();
                auto& chars = str_column->get_chars();
                auto& offsets = str_column->get_offsets();
                // data.resize(input_rows_count);
                offsets.resize(input_rows_count);

                if (auto nullable = check_and_get_column<const ColumnNullable>(
                            *block.get_by_position(arguments[i]).column)) {
                    const auto* int_data =
                            assert_cast<const ColumnInt32*>(nullable->get_nested_column_ptr().get())
                                    ->get_data()
                                    .data();
                    const auto* null_map_data = nullable->get_null_map_data().data();
                    for (size_t j = 0; j < input_rows_count; ++j) {
                        // ignore null
                        if (null_map_data[j]) {
                            offsets[j] = offsets[j - 1];
                        } else {
                            integer_to_char_(j, int_data + j, chars, offsets);
                        }
                    }
                } else {
                    const auto* int_data = assert_cast<const ColumnInt32*>(
                                                   block.get_by_position(arguments[i]).column.get())
                                                   ->get_data()
                                                   .data();
                    for (size_t j = 0; j < input_rows_count; ++j) {
                        integer_to_char_(j, int_data + j, chars, offsets);
                    }
                }
                offsets_list[i - 1] = &str_column->get_offsets();
                chars_list[i - 1] = &str_column->get_chars();
                str_columns[i - 1] = std::move(str_column);
            }
        }

        auto null_map = ColumnUInt8::create(input_rows_count, 0);
        auto res = ColumnString::create();
        auto& res_data = res->get_chars();
        auto& res_offset = res->get_offsets();

        size_t res_reserve_size = 0;
        for (size_t i = 0; i < argument_size - 1; ++i) {
            if (!str_columns[i]) {
                continue;
            }
            if (auto const_column = check_and_get_column<const ColumnConst>(*str_columns[i])) {
                auto str_column =
                        assert_cast<const ColumnString*>(&(const_column->get_data_column()));
                auto& offsets = str_column->get_offsets();
                res_reserve_size += (offsets[0] - offsets[-1]) * input_rows_count;
            } else {
                for (size_t j = 0; j < input_rows_count; ++j) {
                    size_t append = (*offsets_list[i])[j] - (*offsets_list[i])[j - 1];
                    // check whether the output might overflow(unlikely)
                    if (UNLIKELY(UINT_MAX - append < res_reserve_size)) {
                        return Status::BufferAllocFailed(
                                "function char output is too large to allocate");
                    }
                    res_reserve_size += append;
                }
            }
        }
        if ((UNLIKELY(UINT_MAX - input_rows_count < res_reserve_size))) {
            return Status::BufferAllocFailed("function char output is too large to allocate");
        }
        ColumnString::check_chars_length(res_reserve_size, 0);
        res_data.resize(res_reserve_size);
        res_offset.resize(input_rows_count);

        for (size_t i = 0; i < input_rows_count; ++i) {
            int current_length = 0;
            for (size_t j = 0; j < argument_size - 1; ++j) {
                if (!str_columns[j]) {
                    continue;
                }
                if (auto const_column = check_and_get_column<const ColumnConst>(*str_columns[j])) {
                    auto str_column = assert_cast<const ColumnString*, TypeCheckOnRelease::DISABLE>(
                            &(const_column->get_data_column()));
                    auto data_item = str_column->get_data_at(0);
                    memcpy_small_allow_read_write_overflow15(
                            &res_data[res_offset[i - 1]] + current_length, data_item.data,
                            data_item.size);
                    current_length += data_item.size;
                } else {
                    auto& current_offsets = *offsets_list[j];
                    auto& current_chars = *chars_list[j];

                    int size = current_offsets[i] - current_offsets[i - 1];
                    if (size > 0) {
                        memcpy_small_allow_read_write_overflow15(
                                &res_data[res_offset[i - 1]] + current_length,
                                &current_chars[current_offsets[i - 1]], size);
                        current_length += size;
                    }
                }
            }
            res_offset[i] = res_offset[i - 1] + current_length;
        }

        // validate utf8
        auto* null_map_data = null_map->get_data().data();
        for (size_t i = 0; i < input_rows_count; ++i) {
            if (!validate_utf8((const char*)(&res_data[res_offset[i - 1]]),
                               res_offset[i] - res_offset[i - 1])) {
                null_map_data[i] = 1;
            }
        }

        block.get_by_position(result).column =
                ColumnNullable::create(std::move(res), std::move(null_map));
        return Status::OK();
    }

private:
    void integer_to_char_(int line_num, const int* num, ColumnString::Chars& chars,
                          IColumn::Offsets& offsets) const {
        if (0 == *num) {
            chars.push_back('\0');
            offsets[line_num] = offsets[line_num - 1] + 1;
            return;
        }
        const char* bytes = (const char*)(num);
        if constexpr (std::endian::native == std::endian::little) {
            int k = 3;
            for (; k >= 0; --k) {
                if (bytes[k]) {
                    break;
                }
            }
            offsets[line_num] = offsets[line_num - 1] + k + 1;
            for (; k >= 0; --k) {
                chars.push_back(bytes[k] ? bytes[k] : '\0');
            }
        } else if constexpr (std::endian::native == std::endian::big) {
            int k = 0;
            for (; k < 4; ++k) {
                if (bytes[k]) {
                    break;
                }
            }
            offsets[line_num] = offsets[line_num - 1] + 4 - k;
            for (; k < 4; ++k) {
                chars.push_back(bytes[k] ? bytes[k] : '\0');
            }
        } else {
            static_assert(std::endian::native == std::endian::big ||
                                  std::endian::native == std::endian::little,
                          "Unsupported endianness");
        }
    }
};

class FunctionOverlay : public IFunction {
public:
    static constexpr auto name = "overlay";
    static FunctionPtr create() { return std::make_shared<FunctionOverlay>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 4; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        DCHECK_EQ(arguments.size(), 4);

        bool col_const[4];
        ColumnPtr argument_columns[4];
        for (int i = 0; i < 4; ++i) {
            std::tie(argument_columns[i], col_const[i]) =
                    unpack_if_const(block.get_by_position(arguments[i]).column);
        }

        const auto* col_origin = assert_cast<const ColumnString*>(argument_columns[0].get());

        const auto* col_pos =
                assert_cast<const ColumnInt32*>(argument_columns[1].get())->get_data().data();
        const auto* col_len =
                assert_cast<const ColumnInt32*>(argument_columns[2].get())->get_data().data();
        const auto* col_insert = assert_cast<const ColumnString*>(argument_columns[3].get());

        ColumnString::MutablePtr col_res = ColumnString::create();

        // if all input string is ascii, we can use ascii function to handle it
        const bool is_all_ascii = col_origin->is_ascii() && col_insert->is_ascii();
        std::visit(
                [&](auto origin_const, auto pos_const, auto len_const, auto insert_const) {
                    if (is_all_ascii) {
                        vector_ascii<origin_const, pos_const, len_const, insert_const>(
                                col_origin, col_pos, col_len, col_insert, col_res,
                                input_rows_count);
                    } else {
                        vector_utf8<origin_const, pos_const, len_const, insert_const>(
                                col_origin, col_pos, col_len, col_insert, col_res,
                                input_rows_count);
                    }
                },
                vectorized::make_bool_variant(col_const[0]),
                vectorized::make_bool_variant(col_const[1]),
                vectorized::make_bool_variant(col_const[2]),
                vectorized::make_bool_variant(col_const[3]));
        block.replace_by_position(result, std::move(col_res));
        return Status::OK();
    }

private:
    template <bool origin_const, bool pos_const, bool len_const, bool insert_const>
    static void vector_ascii(const ColumnString* col_origin, int const* col_pos, int const* col_len,
                             const ColumnString* col_insert, ColumnString::MutablePtr& col_res,
                             size_t input_rows_count) {
        auto& col_res_chars = col_res->get_chars();
        auto& col_res_offsets = col_res->get_offsets();
        StringRef origin_str, insert_str;
        for (size_t i = 0; i < input_rows_count; i++) {
            origin_str = col_origin->get_data_at(index_check_const<origin_const>(i));
            // pos is 1-based index,so we need to minus 1
            const auto pos = col_pos[index_check_const<pos_const>(i)] - 1;
            const auto len = col_len[index_check_const<len_const>(i)];
            insert_str = col_insert->get_data_at(index_check_const<insert_const>(i));
            const auto origin_size = origin_str.size;
            if (pos >= origin_size || pos < 0) {
                // If pos is not within the length of the string, the original string is returned.
                col_res->insert_data(origin_str.data, origin_str.size);
                continue;
            }
            col_res_chars.insert(origin_str.data,
                                 origin_str.data + pos); // copy origin_str with index 0 to pos - 1
            if (pos + len > origin_size || len < 0) {
                col_res_chars.insert(insert_str.begin(),
                                     insert_str.end()); // copy all of insert_str.
            } else {
                col_res_chars.insert(insert_str.begin(),
                                     insert_str.end()); // copy all of insert_str.
                col_res_chars.insert(
                        origin_str.data + pos + len,
                        origin_str.end()); // copy origin_str from pos+len-1 to the end of the line.
            }
            ColumnString::check_chars_length(col_res_chars.size(), col_res_offsets.size());
            col_res_offsets.push_back(col_res_chars.size());
        }
    }

    template <bool origin_const, bool pos_const, bool len_const, bool insert_const>
    static void vector_utf8(const ColumnString* col_origin, int const* col_pos, int const* col_len,
                            const ColumnString* col_insert, ColumnString::MutablePtr& col_res,
                            size_t input_rows_count) {
        auto& col_res_chars = col_res->get_chars();
        auto& col_res_offsets = col_res->get_offsets();
        StringRef origin_str, insert_str;
        // utf8_origin_offsets is used to store the offset of each utf8 character in the original string.
        // for example, if the original string is "丝多a睿", utf8_origin_offsets will be {0, 3, 6, 7}.
        std::vector<size_t> utf8_origin_offsets;
        for (size_t i = 0; i < input_rows_count; i++) {
            origin_str = col_origin->get_data_at(index_check_const<origin_const>(i));
            // pos is 1-based index,so we need to minus 1
            const auto pos = col_pos[index_check_const<pos_const>(i)] - 1;
            const auto len = col_len[index_check_const<len_const>(i)];
            insert_str = col_insert->get_data_at(index_check_const<insert_const>(i));
            utf8_origin_offsets.clear();

            for (size_t i = 0, char_size = 0; i < origin_str.size; i += char_size) {
                utf8_origin_offsets.push_back(i);
                char_size = get_utf8_byte_length(origin_str.data[i]);
            }

            const size_t utf8_origin_size = utf8_origin_offsets.size();

            if (pos >= utf8_origin_size || pos < 0) {
                // If pos is not within the length of the string, the original string is returned.
                col_res->insert_data(origin_str.data, origin_str.size);
                continue;
            }
            col_res_chars.insert(
                    origin_str.data,
                    origin_str.data +
                            utf8_origin_offsets[pos]); // copy origin_str with index 0 to pos - 1
            if (pos + len >= utf8_origin_size || len < 0) {
                col_res_chars.insert(insert_str.begin(),
                                     insert_str.end()); // copy all of insert_str.
            } else {
                col_res_chars.insert(insert_str.begin(),
                                     insert_str.end()); // copy all of insert_str.
                col_res_chars.insert(
                        origin_str.data + utf8_origin_offsets[pos + len],
                        origin_str.end()); // copy origin_str from pos+len-1 to the end of the line.
            }
            ColumnString::check_chars_length(col_res_chars.size(), col_res_offsets.size());
            col_res_offsets.push_back(col_res_chars.size());
        }
    }
};

class FunctionNgramSearch : public IFunction {
public:
    static constexpr auto name = "ngram_search";
    static FunctionPtr create() { return std::make_shared<FunctionNgramSearch>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeFloat64>();
    }

    // ngram_search(text,pattern,gram_num)
    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        CHECK_EQ(arguments.size(), 3);
        auto col_res = ColumnFloat64::create();
        bool col_const[3];
        ColumnPtr argument_columns[3];
        for (int i = 0; i < 3; ++i) {
            std::tie(argument_columns[i], col_const[i]) =
                    unpack_if_const(block.get_by_position(arguments[i]).column);
        }
        // There is no need to check if the 2-th,3-th parameters are const here because fe has already checked them.
        auto pattern = assert_cast<const ColumnString*>(argument_columns[1].get())->get_data_at(0);
        auto gram_num = assert_cast<const ColumnInt32*>(argument_columns[2].get())->get_element(0);
        const auto* text_col = assert_cast<const ColumnString*>(argument_columns[0].get());

        if (col_const[0]) {
            _execute_impl<true>(text_col, pattern, gram_num, *col_res, input_rows_count);
        } else {
            _execute_impl<false>(text_col, pattern, gram_num, *col_res, input_rows_count);
        }

        block.replace_by_position(result, std::move(col_res));
        return Status::OK();
    }

private:
    using NgramMap = phmap::flat_hash_map<uint32_t, uint8_t>;
    // In the map, the key is the CRC32 hash result of a substring in the string,
    // and the value indicates whether this hash is found in the text or pattern.
    constexpr static auto not_found = 0b00;
    constexpr static auto found_in_pattern = 0b01;
    constexpr static auto found_in_text = 0b10;
    constexpr static auto found_in_pattern_and_text = 0b11;

    uint32_t sub_str_hash(const char* data, int32_t length) const {
        constexpr static uint32_t seed = 0;
        return HashUtil::crc_hash(data, length, seed);
    }

    template <bool column_const>
    void _execute_impl(const ColumnString* text_col, StringRef& pattern, int gram_num,
                       ColumnFloat64& res, size_t size) const {
        auto& res_data = res.get_data();
        res_data.resize_fill(size, 0);
        // If the length of the pattern is less than gram_num, return 0.
        if (pattern.size < gram_num) {
            return;
        }

        // Build a map by pattern string, which will be used repeatedly in the following loop.
        NgramMap pattern_map;
        int pattern_count = get_pattern_set(pattern_map, pattern, gram_num);
        // Each time a loop is executed, the map will be modified, so it needs to be restored afterward.
        std::vector<uint32_t> restore_map;

        for (int i = 0; i < size; i++) {
            auto text = text_col->get_data_at(index_check_const<column_const>(i));
            if (text.size < gram_num) {
                // If the length of the text is less than gram_num, return 0.
                continue;
            }
            restore_map.reserve(text.size);
            auto [text_count, intersection_count] =
                    get_text_set(text, gram_num, pattern_map, restore_map);

            // 2 * |Intersection| / (|text substr set| + |pattern substr set|)
            res_data[i] = 2.0 * intersection_count / (text_count + pattern_count);
        }
    }

    size_t get_pattern_set(NgramMap& pattern_map, StringRef& pattern, int gram_num) const {
        size_t pattern_count = 0;
        for (int i = 0; i + gram_num <= pattern.size; i++) {
            uint32_t cur_hash = sub_str_hash(pattern.data + i, gram_num);
            if (!pattern_map.contains(cur_hash)) {
                pattern_map[cur_hash] = found_in_pattern;
                pattern_count++;
            }
        }
        return pattern_count;
    }

    std::pair<size_t, size_t> get_text_set(StringRef& text, int gram_num, NgramMap& pattern_map,
                                           std::vector<uint32_t>& restore_map) const {
        restore_map.clear();
        //intersection_count indicates a substring both in pattern and text.
        size_t text_count = 0, intersection_count = 0;
        for (int i = 0; i + gram_num <= text.size; i++) {
            uint32_t cur_hash = sub_str_hash(text.data + i, gram_num);
            auto& val = pattern_map[cur_hash];
            if (val == not_found) {
                val ^= found_in_text;
                DCHECK(val == found_in_text);
                // only found in text
                text_count++;
                restore_map.push_back(cur_hash);
            } else if (val == found_in_pattern) {
                val ^= found_in_text;
                DCHECK(val == found_in_pattern_and_text);
                // found in text and pattern
                text_count++;
                intersection_count++;
                restore_map.push_back(cur_hash);
            }
        }
        // Restore the pattern_map.
        for (auto& restore_hash : restore_map) {
            pattern_map[restore_hash] ^= found_in_text;
        }

        return {text_count, intersection_count};
    }
};

class FunctionTranslate : public IFunction {
public:
    static constexpr auto name = "translate";
    static FunctionPtr create() { return std::make_shared<FunctionTranslate>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 3; }

    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return std::make_shared<DataTypeString>();
    };

    DataTypes get_variadic_argument_types_impl() const override {
        return {std::make_shared<DataTypeString>(), std::make_shared<DataTypeString>(),
                std::make_shared<DataTypeString>()};
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        CHECK_EQ(arguments.size(), 3);
        auto col_res = ColumnString::create();
        bool col_const[3];
        ColumnPtr argument_columns[3];
        for (int i = 0; i < 3; ++i) {
            col_const[i] = is_column_const(*block.get_by_position(arguments[i]).column);
        }
        argument_columns[0] = col_const[0] ? static_cast<const ColumnConst&>(
                                                     *block.get_by_position(arguments[0]).column)
                                                     .convert_to_full_column()
                                           : block.get_by_position(arguments[0]).column;
        default_preprocess_parameter_columns(argument_columns, col_const, {1, 2}, block, arguments);

        const auto* col_source = assert_cast<const ColumnString*>(argument_columns[0].get());
        const auto* col_from = assert_cast<const ColumnString*>(argument_columns[1].get());
        const auto* col_to = assert_cast<const ColumnString*>(argument_columns[2].get());

        bool is_ascii = col_source->is_ascii() && col_from->is_ascii() && col_to->is_ascii();
        auto impl_vectors = impl_vectors_utf8<false>;
        if (col_const[1] && col_const[2] && is_ascii) {
            impl_vectors = impl_vectors_ascii<true>;
        } else if (col_const[1] && col_const[2]) {
            impl_vectors = impl_vectors_utf8<true>;
        } else if (is_ascii) {
            impl_vectors = impl_vectors_ascii<false>;
        }
        impl_vectors(col_source, col_from, col_to, col_res.get());
        block.get_by_position(result).column = std::move(col_res);
        return Status::OK();
    }

private:
    template <bool IsConst>
    static void impl_vectors_ascii(const ColumnString* col_source, const ColumnString* col_from,
                                   const ColumnString* col_to, ColumnString* col_res) {
        col_res->get_chars().reserve(col_source->get_chars().size());
        col_res->get_offsets().reserve(col_source->get_offsets().size());
        std::unordered_map<char, char> translate_map;
        if (IsConst) {
            const auto& from_str = col_from->get_data_at(0);
            const auto& to_str = col_to->get_data_at(0);
            translate_map =
                    build_translate_map_ascii(from_str.to_string_view(), to_str.to_string_view());
        }
        for (size_t i = 0; i < col_source->size(); ++i) {
            const auto& source_str = col_source->get_data_at(i);
            if (!IsConst) {
                const auto& from_str = col_from->get_data_at(i);
                const auto& to_str = col_to->get_data_at(i);
                translate_map = build_translate_map_ascii(from_str.to_string_view(),
                                                          to_str.to_string_view());
            }
            auto translated_str = translate_ascii(source_str.to_string_view(), translate_map);
            col_res->insert_data(translated_str.data(), translated_str.size());
        }
    }

    static std::unordered_map<char, char> build_translate_map_ascii(
            const std::string_view& from_str, const std::string_view& to_str) {
        std::unordered_map<char, char> translate_map;
        for (size_t i = 0; i < from_str.size(); ++i) {
            if (translate_map.find(from_str[i]) == translate_map.end()) {
                translate_map[from_str[i]] = i < to_str.size() ? to_str[i] : 0;
            }
        }
        return translate_map;
    }

    static std::string translate_ascii(const std::string_view& source_str,
                                       std::unordered_map<char, char>& translate_map) {
        std::string result;
        result.reserve(source_str.size());
        for (auto const& c : source_str) {
            if (translate_map.find(c) != translate_map.end()) {
                if (translate_map[c]) {
                    result.push_back(translate_map[c]);
                }
            } else {
                result.push_back(c);
            }
        }
        return result;
    }

    template <bool IsConst>
    static void impl_vectors_utf8(const ColumnString* col_source, const ColumnString* col_from,
                                  const ColumnString* col_to, ColumnString* col_res) {
        col_res->get_chars().reserve(col_source->get_chars().size());
        col_res->get_offsets().reserve(col_source->get_offsets().size());
        std::unordered_map<std::string_view, std::string_view> translate_map;
        if (IsConst) {
            const auto& from_str = col_from->get_data_at(0);
            const auto& to_str = col_to->get_data_at(0);
            translate_map =
                    build_translate_map_utf8(from_str.to_string_view(), to_str.to_string_view());
        }
        for (size_t i = 0; i < col_source->size(); ++i) {
            const auto& source_str = col_source->get_data_at(i);
            if (!IsConst) {
                const auto& from_str = col_from->get_data_at(i);
                const auto& to_str = col_to->get_data_at(i);
                translate_map = build_translate_map_utf8(from_str.to_string_view(),
                                                         to_str.to_string_view());
            }
            auto translated_str = translate_utf8(source_str.to_string_view(), translate_map);
            col_res->insert_data(translated_str.data(), translated_str.size());
        }
    }

    static std::unordered_map<std::string_view, std::string_view> build_translate_map_utf8(
            const std::string_view& from_str, const std::string_view& to_str) {
        std::unordered_map<std::string_view, std::string_view> translate_map;
        for (size_t i = 0, from_char_size = 0, j = 0, to_char_size = 0; i < from_str.size();
             i += from_char_size, j += to_char_size) {
            from_char_size = get_utf8_byte_length(from_str[i]);
            to_char_size = j < to_str.size() ? get_utf8_byte_length(to_str[j]) : 0;
            auto from_char = from_str.substr(i, from_char_size);
            if (translate_map.find(from_char) == translate_map.end()) {
                translate_map[from_char] =
                        j < to_str.size() ? to_str.substr(j, to_char_size) : std::string_view();
            }
        }
        return translate_map;
    }

    static std::string translate_utf8(
            const std::string_view& source_str,
            std::unordered_map<std::string_view, std::string_view>& translate_map) {
        std::string result;
        result.reserve(source_str.size());
        for (size_t i = 0, char_size = 0; i < source_str.size(); i += char_size) {
            char_size = get_utf8_byte_length(source_str[i]);
            auto c = source_str.substr(i, char_size);
            if (translate_map.find(c) != translate_map.end()) {
                if (!translate_map[c].empty()) {
                    result.append(translate_map[c]);
                }
            } else {
                result.append(c);
            }
        }
        return result;
    }
};

/// xpath_string(xml, xpath) -> String
/// Returns the text content of the first node that matches the XPath expression.
/// Returns NULL if either xml or xpath is NULL.
/// Returns empty string if the XPath expression matches no nodes.
/// The text content includes the node and all its descendants.
/// Example:
///   xpath_string('<a><b>b1</b><b>b2</b></a>', '/a/b[1]') = 'b1'
///   xpath_string('<a><b>b1</b><b>b2</b></a>', '/a/b[2]') = 'b2'
///   xpath_string('<a><b>b1</b><b>b2</b></a>', '/a/c') = ''
///   xpath_string('invalid xml', '/a/b[1]') = NULL
///   xpath_string(NULL, '/a/b[1]') = NULL
///   xpath_string('<a><b>b1</b><b>b2</b></a>', NULL) = NULL
class FunctionXPathString : public IFunction {
public:
    static constexpr auto name = "xpath_string";
    static FunctionPtr create() { return std::make_shared<FunctionXPathString>(); }
    String get_name() const override { return name; }
    size_t get_number_of_arguments() const override { return 2; }
    DataTypePtr get_return_type_impl(const DataTypes& arguments) const override {
        return make_nullable(std::make_shared<DataTypeString>());
    }

    Status execute_impl(FunctionContext* context, Block& block, const ColumnNumbers& arguments,
                        uint32_t result, size_t input_rows_count) const override {
        CHECK_EQ(arguments.size(), 2);
        auto col_res = ColumnNullable::create(ColumnString::create(), ColumnUInt8::create());
        const auto& [left_col, left_const] =
                unpack_if_const(block.get_by_position(arguments[0]).column);
        const auto& [right_col, right_const] =
                unpack_if_const(block.get_by_position(arguments[1]).column);
        const auto& xml_col = *assert_cast<const ColumnString*>(left_col.get());
        const auto& xpath_col = *assert_cast<const ColumnString*>(right_col.get());

        Status status;
        if (left_const && right_const) {
            status = execute_vector<true, true>(input_rows_count, xml_col, xpath_col, *col_res);
        } else if (left_const) {
            status = execute_vector<true, false>(input_rows_count, xml_col, xpath_col, *col_res);
        } else if (right_const) {
            status = execute_vector<false, true>(input_rows_count, xml_col, xpath_col, *col_res);
        } else {
            status = execute_vector<false, false>(input_rows_count, xml_col, xpath_col, *col_res);
        }
        if (!status.ok()) {
            return status;
        }

        block.get_by_position(result).column = std::move(col_res);
        return Status::OK();
    }

private:
    static Status parse_xml(const StringRef& xml_str, pugi::xml_document& xml_doc) {
        pugi::xml_parse_result result = xml_doc.load_buffer(xml_str.data, xml_str.size);
        if (!result) {
            return Status::InvalidArgument("Function {} failed to parse XML string: {}", name,
                                           result.description());
        }
        return Status::OK();
    }

    static Status build_xpath_query(const StringRef& xpath_str, pugi::xpath_query& xpath_query) {
        // xpath_query will throws xpath_exception on compilation errors.
        try {
            // NOTE!!!: don't use to_string_view(), because xpath_str maybe not null-terminated
            xpath_query = pugi::xpath_query(xpath_str.to_string().c_str());
        } catch (const pugi::xpath_exception& e) {
            return Status::InvalidArgument("Function {} failed to build XPath query: {}", name,
                                           e.what());
        }
        return Status::OK();
    }

    template <bool left_const, bool right_const>
    static Status execute_vector(const size_t input_rows_count, const ColumnString& xml_col,
                                 const ColumnString& xpath_col, ColumnNullable& res_col) {
        pugi::xml_document xml_doc;
        pugi::xpath_query xpath_query;
        // first check right_const, because we want to check empty input first
        if constexpr (right_const) {
            auto xpath_str = xpath_col.get_data_at(0);
            if (xpath_str.empty()) {
                // should return null if xpath_str is empty
                res_col.insert_many_defaults(input_rows_count);
                return Status::OK();
            }
            RETURN_IF_ERROR(build_xpath_query(xpath_str, xpath_query));
        }
        if constexpr (left_const) {
            auto xml_str = xml_col.get_data_at(0);
            if (xml_str.empty()) {
                // should return null if xml_str is empty
                res_col.insert_many_defaults(input_rows_count);
                return Status::OK();
            }
            RETURN_IF_ERROR(parse_xml(xml_str, xml_doc));
        }

        for (size_t i = 0; i < input_rows_count; ++i) {
            if constexpr (!right_const) {
                auto xpath_str = xpath_col.get_data_at(i);
                if (xpath_str.empty()) {
                    // should return null if xpath_str is empty
                    res_col.insert_default();
                    continue;
                }
                RETURN_IF_ERROR(build_xpath_query(xpath_str, xpath_query));
            }
            if constexpr (!left_const) {
                auto xml_str = xml_col.get_data_at(i);
                if (xml_str.empty()) {
                    // should return null if xml_str is empty
                    res_col.insert_default();
                    continue;
                }
                RETURN_IF_ERROR(parse_xml(xml_str, xml_doc));
            }
            std::string text;
            try {
                text = xpath_query.evaluate_string(xml_doc);
            } catch (const pugi::xpath_exception& e) {
                return Status::InvalidArgument("Function {} failed to query XPath string: {}", name,
                                               e.what());
            }
            res_col.insert_data(text.data(), text.size());
        }
        return Status::OK();
    }
};

} // namespace doris::vectorized
