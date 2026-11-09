// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/property.hpp"
#include "engine/core/meta/registry_fwd.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <span>
#include <vector>

namespace gneiss::engine::meta {
namespace detail {
struct type_factory;
}
/** 只读类型视图；持有注册表，不持有对象。借出的元数据不得超过本视图寿命。 */
class type {
public:
  type() = default;
  [[nodiscard]] type_kind kind() const noexcept { return kind_; }
  [[nodiscard]] bool is_enum() const noexcept { return enumeration_; }
  [[nodiscard]] engine::type_id id() const noexcept { return id_; }
  [[nodiscard]] engine::guid guid() const noexcept { return enumeration_ ? engine::guid{} : guid_; }
  [[nodiscard]] value_kind scalar_kind() const noexcept { return scalar_; }
  [[nodiscard]] GNEISS_API result<std::span<const enum_entry>> enum_entries() const noexcept;
  [[nodiscard]] GNEISS_API result<property_range> properties() const noexcept;
  [[nodiscard]] GNEISS_API bool is_fixed_sequence() const noexcept;
  [[nodiscard]] GNEISS_API bool has_sequence_identity() const noexcept;
  /** sequence/optional/reference 返回元素或静态目标类型，associative 返回映射值类型。 */
  [[nodiscard]] GNEISS_API result<type> element_type() const noexcept;

private:
  friend class const_instance;
  friend struct detail::type_factory;
  registry_ref registry_;
  const detail::type_ops* access_{};
  engine::type_id id_{};
  engine::guid guid_{};
  type_kind kind_ = type_kind::invalid;
  value_kind scalar_ = value_kind::invalid;
  bool enumeration_ = false;
};
/** 名称借用冻结注册表；类型视图持有该注册表，属性标记不属于共享类型。 */
struct property_info {
  property_id id{};
  /** 属性声明所属类型；继承访问须先调整到该基类子对象。 */
  engine::guid declaring_type{};
  property_flags flags = property_flags::none;
  std::string_view name{};
  property_capabilities capabilities = property_capabilities::none;
  meta::type type{};
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
