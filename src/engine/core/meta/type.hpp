// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/property.hpp"
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/type_id.hpp"
#include <span>
#include <vector>

namespace gneiss::engine::meta {
namespace detail {
struct type_factory;
}
/** 只读类型视图；持有注册表，不持有对象。借出的元数据不得超过本视图寿命。 */
class GNEISS_API type {
public:
  type() = default;
  [[nodiscard]] type_kind kind() const noexcept { return kind_; }
  [[nodiscard]] bool is_enum() const noexcept { return enumeration_; }
  [[nodiscard]] engine::type_id id() const noexcept { return id_; }
  [[nodiscard]] engine::guid guid() const noexcept { return enumeration_ ? engine::guid{} : guid_; }
  [[nodiscard]] value_kind scalar_kind() const noexcept { return scalar_; }
  [[nodiscard]] result<std::span<const enum_entry>> enum_entries() const noexcept;
  [[nodiscard]] result<std::vector<property_info>> properties() const noexcept;
  /** sequence/optional 返回元素类型，associative 返回映射值类型。 */
  [[nodiscard]] result<type> element_type() const noexcept;

private:
  friend class instance;
  friend struct detail::type_factory;
  registry_ref registry_;
  const detail::type_access* access_{};
  engine::type_id id_{};
  engine::guid guid_{};
  type_kind kind_ = type_kind::invalid;
  value_kind scalar_ = value_kind::invalid;
  bool enumeration_ = false;
};
/** 名称借用冻结注册表；类型视图持有该注册表，属性标记不属于共享类型。 */
struct property_info {
  property_id id{};
  property_flags flags = property_flags::none;
  std::string_view name{};
  property_capabilities capabilities = property_capabilities::none;
  meta::type type{};
};
struct type_definition {
  engine::guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  std::span<const property_definition> properties{};
  serialization_binding serialization{};
};
/** 自有字段数组；名称借用冻结 Registry 的文本，复制/移动不产生临时数组悬空。 */
struct type_info {
  engine::guid guid{};
  engine::type_id type_id{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::vector<property_info> properties{};
};
} // namespace gneiss::engine::meta
