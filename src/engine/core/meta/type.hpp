// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/property.hpp"
#include "engine/core/meta/registry_fwd.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <span>

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
  /** 注册对象名称；没有注册名称时返回空字符串，借用期间须保活句柄。 */
  [[nodiscard]] GNEISS_API std::string_view name() const noexcept;
  /** 未登记版本的类型返回零。 */
  [[nodiscard]] GNEISS_API std::uint32_t schema_version() const noexcept;
  [[nodiscard]] GNEISS_API result<property_range> properties() const noexcept;
  [[nodiscard]] GNEISS_API result<property> find_property(property_id) const noexcept;
  [[nodiscard]] GNEISS_API result<property> find_property(std::string_view) const noexcept;
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
} // namespace gneiss::engine::meta
