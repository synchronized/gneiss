// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/value.hpp"
namespace gneiss::engine::serialization {
class context;
/** 节点与键名借用后端；游标只在所属 reader 和输入数据存活期间有效。 */
class reader {
public:
  using node = const void*;
  struct cursor {
    std::size_t index{}, count{};
    node current{}, owner{};
  };
  struct entry {
    std::string_view key;
    node data{};
    explicit operator bool() const noexcept { return data != nullptr; }
  };
  virtual ~reader() = default;
  virtual node root() const = 0;
  virtual bool is_object(node) const = 0;
  virtual bool is_array(node) const = 0;
  virtual bool is_null(node) const = 0;
  virtual bool is_string(node) const = 0;
  virtual std::string_view string(node) const = 0;
  virtual std::size_t size(node) const = 0;
  virtual node member(node, std::string_view) const = 0;
  virtual cursor iterate(node) const = 0;
  virtual node next_element(cursor&) const = 0;
  virtual entry next_member(cursor&) const = 0;
  virtual result<value, meta::serialization_diagnostic> scalar(node, value_kind) const = 0;
  virtual bool has_custom(const meta::serialization_binding&) const { return false; }
  virtual result<ref<void>, meta::serialization_diagnostic>
  custom(const context&, type_id, const meta::serialization_binding&, node) {
    return meta::serialization_diagnostic{.error = error::unsupported};
  }
};
}
