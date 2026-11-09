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
struct type_record;
}
/** 只读类型视图；持有注册表，不持有对象。借出的元数据不得超过本视图寿命。 */
class type {
public:
  type() = default;
  /** 借用冻结扩展元数据；缺少此类扩展返回 nullptr。 */
  template <class T> [[nodiscard]] const T* extension() const noexcept {
    return static_cast<const T*>(extension(runtime_type_of<T>()));
  }
  [[nodiscard]] GNEISS_API const void* extension(engine::type_id) const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return access_ || record_; }
  /** 是否关联冻结注册数据；原生类型有效并不要求已注册。 */
  [[nodiscard]] bool registered() const noexcept { return record_ != nullptr; }
  [[nodiscard]] GNEISS_API type_kind kind() const noexcept;
  [[nodiscard]] GNEISS_API bool is_enum() const noexcept;
  [[nodiscard]] GNEISS_API engine::type_id id() const noexcept;
  [[nodiscard]] GNEISS_API engine::guid guid() const noexcept;
  [[nodiscard]] GNEISS_API value_kind scalar_kind() const noexcept;
  /** 借用已注册枚举项；无注册记录或非枚举返回空范围，寿命受本句柄保护。 */
  [[nodiscard]] GNEISS_API std::span<const enum_entry> enum_entries() const noexcept;
  /** 注册对象名称；没有注册名称时返回空字符串，借用期间须保活句柄。 */
  [[nodiscard]] GNEISS_API std::string_view name() const noexcept;
  /** 未登记版本的类型返回零。 */
  [[nodiscard]] GNEISS_API std::uint32_t schema_version() const noexcept;
  /** 未注册或非对象类型返回空范围；是否注册由 registered 判断。 */
  [[nodiscard]] GNEISS_API property_range properties() const noexcept;
  [[nodiscard]] GNEISS_API property find_property(property_id) const noexcept;
  [[nodiscard]] GNEISS_API property find_property(std::string_view) const noexcept;
  [[nodiscard]] GNEISS_API bool is_fixed_sequence() const noexcept;
  [[nodiscard]] GNEISS_API bool has_sequence_identity() const noexcept;
  /** 同注册表内按类型身份比较；跨表比较身份时显式使用 id/guid。 */
  friend GNEISS_API bool operator==(const type&, const type&) noexcept;
  /** 无元素能力返回空句柄。
   * sequence/optional/reference 返回元素或静态目标类型，associative 返回映射值类型。 */
  [[nodiscard]] GNEISS_API type element_type() const noexcept;

private:
  template <bool> friend class basic_instance;
  friend struct detail::type_factory;
  registry_ref registry_;
  const detail::type_ops* access_{};
  const detail::type_record* record_{};
};
namespace detail {
GNEISS_API meta::type resolve_type(const registry_ref&, const type_ops&) noexcept;
}
} // namespace gneiss::engine::meta
