// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#define GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#include "engine/core/result.hpp"
#include "engine/core/traits/type_identity.hpp"
#include "property_registry.hpp"
#include "serialization.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gneiss::engine {
struct field_definition {
  field_id id{};
  engine::type_guid type_guid{};
  field_flags flags = field_flags::none;
  std::string_view name{};
};
struct type_definition {
  engine::type_guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name{};
  std::span<const field_definition> fields{};
  serialization_binding serialization{};
};
/** 回调借用实际类型的对象指针；调用方保证对象存活、类型正确和线程访问合法。
 * setter 必须先校验再修改，失败保持对象；注册表不复制对象或回滚任意回调副作用。
 * 上下文和对象借用；getter 输出拥有数据的 value，setter 只在调用期间借用输入 value。 */
struct property_binding {
  gneiss::engine::value_kind kind = gneiss::engine::value_kind::invalid;
  result (*getter)(void*, const void*, gneiss::engine::value&) = nullptr;
  result (*setter)(void*, void*, const gneiss::engine::value&) = nullptr;
  void* user_data{};
};
[[nodiscard]] inline bool valid_type_id(type_guid id) noexcept { return static_cast<bool>(id); }
/** 创建同一实现的原生对象，不注册公共 RID；失败保持原输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::result create_native(registry_ref& output) noexcept;
/** 深拷贝元数据；重复 GUID 或非空运行期 ID 返回 invalid_argument，冻结后禁止注册。 */
[[nodiscard]] GNEISS_API gneiss::engine::result register_type(const registry_ref& state,
                                                              const type_definition& desc) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
bind_property(const registry_ref& state, type_guid type_id, field_id field,
              const property_binding& desc) noexcept;
/** 同时登记全部字段绑定；bindings 与 fields 顺序对应，失败不登记类型。 */
[[nodiscard]] GNEISS_API result register_type(const registry_ref&, const type_definition&,
                                              std::span<const property_binding>) noexcept;
/** 从规范化 C++ 类型提取身份；显式身份与 traits 冲突时拒绝注册。 */
template <class T>
[[nodiscard]] result register_type(const registry_ref& registry, type_definition definition,
                                   std::span<const property_binding> bindings = {}) noexcept {
  using type = std::remove_cvref_t<T>;
  const auto guid = type_identity<type>::guid();
  const auto name = type_identity<type>::name();
  if ((guid && definition.guid && guid != definition.guid) ||
      (!name.empty() && !definition.name.empty() && name != definition.name))
    return result::invalid_argument;
  if (!definition.guid)
    definition.guid = guid;
  if (definition.name.empty())
    definition.name = name;
  const auto id = runtime_type_of<type>();
  if (!id || (definition.type_id && definition.type_id != id))
    return result::invalid_argument;
  definition.type_id = id;
  return register_type(registry, definition, bindings);
}
[[nodiscard]] GNEISS_API gneiss::engine::result freeze(const registry_ref& state) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result is_frozen(const registry_ref& state,
                                                          bool& output) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
get_property(const registry_ref& state, type_guid type_id, std::uint32_t field_id,
             const void* object, gneiss::engine::value& output) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
set_property(const registry_ref& state, type_guid type_id, std::uint32_t field_id, void* object,
             const gneiss::engine::value& value) noexcept;
} // namespace gneiss::engine
#endif
