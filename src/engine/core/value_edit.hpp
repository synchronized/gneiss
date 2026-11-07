// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/property_path.hpp"
#include "engine/core/result.hpp"
namespace gneiss::engine {
struct set_value_operation {
  static constexpr std::string_view type_name = "set_value_operation";
  static constexpr guid type_guid = guid::literal("bfab54df-7d47-4912-b98d-ec972c0ae5fa");
  value content;
};
struct insert_value_operation {
  static constexpr std::string_view type_name = "insert_value_operation";
  static constexpr guid type_guid = guid::literal("5e956dc8-9ff8-4e47-bfa6-aea1e630f163");
  guid element;
  guid before;
  value content;
};
struct erase_value_operation {
  static constexpr std::string_view type_name = "erase_value_operation";
  static constexpr guid type_guid = guid::literal("41e930e8-be60-4dae-857a-a466cdb33633");
  guid element;
};
struct move_value_operation {
  static constexpr std::string_view type_name = "move_value_operation";
  static constexpr guid type_guid = guid::literal("6590592b-7430-43f5-b267-5f112eed661d");
  guid element;
  guid before;
};
struct put_value_operation {
  static constexpr std::string_view type_name = "put_value_operation";
  static constexpr guid type_guid = guid::literal("f5a2c150-bca4-4bb9-ba13-de09e5f240a4");
  value key;
  value content;
};
struct erase_key_operation {
  static constexpr std::string_view type_name = "erase_key_operation";
  static constexpr guid type_guid = guid::literal("87b334d6-ce2f-4b42-9c2d-c0eb08d73726");
  value key;
};
using value_operation =
    std::variant<set_value_operation, insert_value_operation, erase_value_operation,
                 move_value_operation, put_value_operation, erase_key_operation>;
struct value_edit {
  static constexpr std::string_view type_name = "value_edit";
  static constexpr guid type_guid = guid::literal("0e42bd59-a518-4513-a728-85300fcf25cb");
  meta::property_path path;
  value_operation operation;
};
struct value_edit_error {
  std::size_t operation{};
  engine::error error{engine::error::success};
};
/** 候选根上执行整组操作；失败不发布候选，原根及共享节点始终保持不变。 */
GNEISS_API result<value, value_edit_error> apply_value_edits(const value&,
                                                             std::span<const value_edit>) noexcept;
struct value_edit_replay {
  value root;
  std::vector<value_edit_error> conflicts;
};
/** Prefab 重放：保留失败操作的索引，其他独立操作继续执行。 */
GNEISS_API result<value_edit_replay> replay_value_edits(const value&,
                                                        std::span<const value_edit>) noexcept;
/** 普通列表整体替换，有稳定身份的列表按身份增删移动；引用只比较身份。 */
GNEISS_API result<std::vector<value_edit>> diff_values(const value&, const value&) noexcept;
}

namespace gneiss::engine::meta {
template <> struct variant_case_traits<value_operation> {
  static constexpr std::array<std::string_view, 6> names{"set",  "insert",    "erase",
                                                         "move", "put_entry", "erase_entry"};
};
}
