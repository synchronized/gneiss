// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/meta/export.hpp>

#include <gneiss/engine/core/meta/property.hpp>
#include <gneiss/engine/core/meta/registry_fwd.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <gneiss/engine/core/base/result.hpp>

#include <span>

namespace gneiss::meta {
namespace cap {
class representation;
class sequence;
class variant;
class enumeration;
class object;
template <type_kind, template <bool> class> class container;
}
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
  [[nodiscard]] GNEISS_META_API const void* extension(gneiss::type_id) const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return access_ || record_; }
  /** 是否关联冻结注册数据；原生类型有效并不要求已注册。 */
  [[nodiscard]] bool registered() const noexcept { return record_ != nullptr; }
  [[nodiscard]] GNEISS_META_API type_kind kind() const noexcept;
  [[nodiscard]] GNEISS_META_API bool is_enum() const noexcept;
  [[nodiscard]] GNEISS_META_API gneiss::type_id id() const noexcept;
  [[nodiscard]] GNEISS_META_API gneiss::guid guid() const noexcept;
  [[nodiscard]] GNEISS_META_API value_kind scalar_kind() const noexcept;
  /** 注册对象名称；没有注册名称时返回空字符串，借用期间须保活句柄。 */
  [[nodiscard]] GNEISS_META_API std::string_view name() const noexcept;
  /** 未登记版本的类型返回零。 */
  [[nodiscard]] GNEISS_META_API std::uint32_t schema_version() const noexcept;
  /** 查询静态能力；不支持时返回空 capability，不要求已有对象或已注册字段。 */
  template <class Capability> [[nodiscard]] Capability capability() const {
    return Capability{*this};
  }

  /** 同注册表内按类型身份比较；跨表比较身份时显式使用 id/guid。 */
  friend GNEISS_META_API bool operator==(const type&, const type&) noexcept;

private:
  template <bool> friend class basic_instance;
  friend struct detail::type_factory;
  friend class cap::representation;
  friend class cap::sequence;
  friend class cap::variant;
  friend class cap::enumeration;
  friend class cap::object;
  template <type_kind, template <bool> class> friend class cap::container;
  registry_ref registry_;
  const type_ops* access_{};
  const detail::type_record* record_{};
};
namespace detail {
GNEISS_META_API meta::type resolve_type(const registry_ref&, const type_ops&);
}
} // namespace gneiss::meta
