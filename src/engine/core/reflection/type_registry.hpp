// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#define GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#include "engine/core/result.hpp"
#include "property_registry.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gneiss::engine {
/** 借用候选数据，仅修改该数据并校验；禁止场景副作用和普通业务通知。 */
using candidate_property_setter = gneiss::engine::result (*)(void* user_data, void* candidate,
                                                             const gneiss::property_value& value);
struct field_definition {
  field_id id{};
  engine::type_guid type_guid{};
  field_flags flags = field_flags::none;
  std::string_view name;
};
struct type_definition {
  engine::type_guid guid{};
  /** 可空；非空时必须与 guid 一一对应，不持久化。 */
  engine::type_id type_id{};
  std::uint32_t schema_version{};
  std::string_view name;
  std::span<const field_definition> fields;
};
/** 回调使用原生属性值；上下文及属性文本借用，不隐式持有用户对象。
 * getter 返回的文本须至少存活到调用方完成复制，setter 的文本仅在调用期间有效。 */
struct property_binding {
  property_kind kind = property_kind::invalid;
  result (*getter)(void*, property_target, gneiss::property_value*) = nullptr;
  result (*setter)(void*, property_target, const gneiss::property_value*) = nullptr;
  void* user_data{};
  candidate_property_setter candidate_setter{};
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
/** 模板入口只绑定 C++ 身份，名称、GUID 和字段仍由调用方显式提供。 */
template <class T>
[[nodiscard]] result register_type(const registry_ref& registry,
                                   type_definition definition) noexcept {
  const auto id = runtime_type_of<T>();
  if (!id || (definition.type_id && definition.type_id != id))
    return result::invalid_argument;
  definition.type_id = id;
  return register_type(registry, definition);
}
[[nodiscard]] GNEISS_API gneiss::engine::result freeze(const registry_ref& state) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result is_frozen(const registry_ref& state,
                                                          bool& output) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
get_property(const registry_ref& state, type_guid type_id, std::uint32_t field_id,
             property_target target, gneiss::property_value& output) noexcept;
[[nodiscard]] GNEISS_API gneiss::engine::result
set_property(const registry_ref& state, type_guid type_id, std::uint32_t field_id,
             property_target target, const gneiss::property_value& value) noexcept;
/** 不回退到运行时 setter。candidate 为字段所属类型的可写候选数据，调用期间借用。
 * 调用方独占候选，失败后丢弃整份候选；异常转换为 INTERNAL，不发送通知。
 */
[[nodiscard]] GNEISS_API gneiss::engine::result
set_candidate_property(const registry_ref& state, type_guid type_id, std::uint32_t field_id,
                       void* candidate, const gneiss::property_value& value) noexcept;
} // namespace gneiss::engine
#endif
