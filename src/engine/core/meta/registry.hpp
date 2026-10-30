// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/type.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <memory>

namespace gneiss::engine::meta {
/** 深拷贝枚举显示项；同一 C++ 类型只登记一次，冻结后禁止修改。 */
[[nodiscard]] GNEISS_API error register_enum(const registry_ref&, engine::type_id, value_kind,
                                             std::span<const enum_entry>) noexcept;
/** 冻结后借用枚举项；未知枚举返回 not_found。 */
GNEISS_API result<std::span<const enum_entry>> enum_entries(const registry_ref&,
                                                            engine::type_id) noexcept;
/** 创建独立注册表，成功结果持有共享所有者。 */
[[nodiscard]] GNEISS_API result<registry_ref> create_registry() noexcept;
/** 进程共享的默认注册表；仅创建，不自动注册或冻结，由引擎模块持有至退出。 */
[[nodiscard]] GNEISS_API result<registry_ref> default_registry() noexcept;
/** 深拷贝元数据；重复 GUID 或非空运行期 ID 返回 invalid_argument，冻结后禁止注册。 */
[[nodiscard]] GNEISS_API error register_type(const registry_ref& state,
                                             const type_definition& desc) noexcept;
[[nodiscard]] GNEISS_API error bind_property(const registry_ref& state, guid type_id,
                                             property_id property,
                                             const property_binding& desc) noexcept;
/** 同时登记全部字段绑定；bindings 与 properties 顺序对应，失败不登记类型。 */
[[nodiscard]] GNEISS_API error register_type(const registry_ref&, const type_definition&,
                                             std::span<const property_binding>) noexcept;
[[nodiscard]] GNEISS_API error freeze(const registry_ref& state) noexcept;
[[nodiscard]] GNEISS_API result<bool> is_frozen(const registry_ref& state) noexcept;
/** 冻结后查询函数表，未登记能力返回 unsupported。 */
[[nodiscard]] GNEISS_API result<serialization_binding> find_serialization(const registry_ref&,
                                                                          type_id) noexcept;
/** 冻结后返回类型数量；未冻结返回 not_ready。 */
[[nodiscard]] GNEISS_API result<std::uint32_t> type_count(const registry_ref& registry) noexcept;
/** 按稳定 GUID 字节顺序枚举冻结元数据，名称借用 Registry。 */
[[nodiscard]] GNEISS_API result<type_info> describe_type_at(const registry_ref& registry,
                                                            std::uint32_t index) noexcept;
/** 元数据名称借用已冻结 Registry。 */
[[nodiscard]] GNEISS_API result<type_info> describe_type(const registry_ref& registry,
                                                         engine::guid type) noexcept;
/** 按当前程序的 C++ 类型身份查询同一份冻结元数据；零或未知 ID 返回 not_found。 */
[[nodiscard]] GNEISS_API result<type_info> describe_type(const registry_ref& registry,
                                                         engine::type_id type) noexcept;
template <class T>
[[nodiscard]] result<type_info> describe_type(const registry_ref& registry) noexcept {
  return describe_type(registry, runtime_type_of<T>());
}
/** 字段名称借用已冻结 Registry。 */
[[nodiscard]] GNEISS_API result<property_info>
describe_property(const registry_ref& registry, engine::guid type, property_id property) noexcept;
/** 读取自有快照。冻结后的 Registry 可共享，对象的类型、寿命和线程访问由调用方保证。
 */
[[nodiscard]] GNEISS_API result<value> read_property(const registry_ref& registry,
                                                     engine::guid type, property_id property,
                                                     const void* object) noexcept;
/** 输入在调用期间借用；沿用访问器的校验、线程与错误语义，不跨线程投递。 */
[[nodiscard]] GNEISS_API error write_property(const registry_ref& registry, engine::guid type,
                                              property_id property, void* object,
                                              const value& value) noexcept;

/** 查询嵌套成员访问器；地址借用和线程约束与父对象一致，非对象属性返回 unsupported。 */
GNEISS_API result<property_binding> find_object_property(const registry_ref&, guid,
                                                         property_id) noexcept;

/** 以下重载使用默认注册表；错误与显式 registry 接口一致，不隐式冻结。 */
[[nodiscard]] inline error register_type(const type_definition& desc) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return register_type(*registry, desc);
}
[[nodiscard]] inline error register_type(const type_definition& desc,
                                         std::span<const property_binding> bindings) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return register_type(*registry, desc, bindings);
}
[[nodiscard]] inline error bind_property(guid type, property_id property,
                                         const property_binding& binding) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return bind_property(*registry, type, property, binding);
}
[[nodiscard]] inline error freeze() noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return freeze(*registry);
}
[[nodiscard]] inline result<bool> is_frozen() noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return is_frozen(*registry);
}
[[nodiscard]] inline result<serialization_binding> find_serialization(type_id type) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return find_serialization(*registry, type);
}
[[nodiscard]] inline result<std::uint32_t> type_count() noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return type_count(*registry);
}
[[nodiscard]] inline result<type_info> describe_type_at(std::uint32_t index) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return describe_type_at(*registry, index);
}
[[nodiscard]] inline result<type_info> describe_type(guid type) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return describe_type(*registry, type);
}
[[nodiscard]] inline result<type_info> describe_type(type_id type) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return describe_type(*registry, type);
}
[[nodiscard]] inline result<property_info> describe_property(guid type,
                                                             property_id property) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return describe_property(*registry, type, property);
}
[[nodiscard]] inline result<value> read_property(guid type, property_id property,
                                                 const void* object) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return read_property(*registry, type, property, object);
}
[[nodiscard]] inline error write_property(guid type, property_id property, void* object,
                                          const value& input) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return write_property(*registry, type, property, object, input);
}
template <class T> [[nodiscard]] result<type_info> describe_type() noexcept {
  return describe_type(runtime_type_of<T>());
}
} // namespace gneiss::engine::meta
