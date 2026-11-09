// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/detail/type_ops.hpp"
#include "engine/core/meta/registry_fwd.hpp"
#include "engine/core/meta/type.hpp"
#include "engine/core/meta/type_definition.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <memory>

namespace gneiss::engine::meta {
/** 查询冻结注册表；未发布或未找到返回空句柄，基础值不需重复登记。 */
GNEISS_API meta::type find_type(const registry_ref&, engine::type_id) noexcept;
GNEISS_API meta::type find_type(const registry_ref&, engine::guid) noexcept;
template <class T> meta::type type_of(const registry_ref& registry) noexcept {
  return detail::resolve_type(registry, detail::ops_of<std::remove_cvref_t<T>>());
}
/** 深拷贝枚举显示项；同一 C++ 类型只登记一次，冻结后禁止修改。 */
[[nodiscard]] GNEISS_API error register_enum(const registry_ref&, const enum_definition&);
/** 创建独立注册表，成功结果持有共享所有者。 */
[[nodiscard]] GNEISS_API result<registry_ref> create_registry() noexcept;
/** 进程共享的默认注册表；仅创建，不自动注册或冻结，由引擎模块持有至退出。 */
[[nodiscard]] GNEISS_API result<registry_ref> default_registry() noexcept;
/** 深拷贝元数据；重复 GUID 或非空运行期 ID 返回 invalid_argument，冻结后禁止注册。 */
[[nodiscard]] GNEISS_API error register_type(const registry_ref& state,
                                             const type_definition& desc);
[[nodiscard]] GNEISS_API error bind_property(const registry_ref& state, guid type_id,
                                             property_id property,
                                             const property_binding& desc) noexcept;
/** 同时登记全部字段绑定；bindings 与 properties 顺序对应，失败不登记类型。 */
[[nodiscard]] GNEISS_API error register_type(const registry_ref&, const type_definition&,
                                             std::span<const property_binding>);
[[nodiscard]] GNEISS_API error freeze(const registry_ref& state) noexcept;
[[nodiscard]] GNEISS_API result<bool> is_frozen(const registry_ref& state) noexcept;
/** 冻结后查询类型构造与继承能力。 */
GNEISS_API result<object_ops> find_object_ops(const registry_ref&, type_id) noexcept;
inline result<object_ops> find_object_ops(type_id id) noexcept {
  auto registry = default_registry();
  return registry ? find_object_ops(*registry, id) : result<object_ops>{registry.error()};
}
/** 冻结后返回类型数量；未冻结返回 not_ready。 */
[[nodiscard]] GNEISS_API result<std::uint32_t> type_count(const registry_ref& registry) noexcept;
/** 按稳定 GUID 字节顺序枚举冻结注册对象，返回保活注册表的类型句柄。 */
[[nodiscard]] GNEISS_API result<meta::type> type_at(const registry_ref&, std::uint32_t) noexcept;
/** 字段名称借用已冻结 Registry。 */
[[nodiscard]] GNEISS_API property find_property(const registry_ref& registry, engine::guid type,
                                                property_id property) noexcept;
/** 读取自有快照。冻结后的 Registry 可共享，对象的类型、寿命和线程访问由调用方保证。
 */
[[nodiscard]] GNEISS_API result<value> read_property(const registry_ref& registry,
                                                     engine::guid type, property_id property,
                                                     const void* object);
/** 输入在调用期间借用；沿用访问器的校验、线程与错误语义，不跨线程投递。 */
[[nodiscard]] GNEISS_API error write_property(const registry_ref& registry, engine::guid type,
                                              property_id property, void* object,
                                              const value& value);

/** 查询嵌套成员访问器；地址借用和线程约束与父对象一致，非对象属性返回 unsupported。 */
GNEISS_API result<property_binding> find_object_property(const registry_ref&, guid,
                                                         property_id) noexcept;

/** 原生实例按进程类型身份访问；不要求持久 GUID。 */
GNEISS_API result<value> read_property(const registry_ref&, engine::type_id, property_id,
                                       const void*);
GNEISS_API error write_property(const registry_ref&, engine::type_id, property_id, void*,
                                const value&);
GNEISS_API result<property_binding> find_object_property(const registry_ref&, engine::type_id,
                                                         property_id) noexcept;

inline error register_enum(const enum_definition& definition) {
  auto registry = default_registry();
  return registry ? register_enum(*registry, definition) : registry.error();
}
inline result<property_binding> find_object_property(guid type, property_id property) noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return find_object_property(*registry, type, property);
}

template <class T> meta::type type_of() noexcept {
  auto registry = default_registry();
  return type_of<T>(registry ? *registry : registry_ref{});
}
inline meta::type find_type(engine::type_id id) noexcept {
  auto registry = default_registry();
  if (!registry)
    return {};
  return find_type(*registry, id);
}
inline meta::type find_type(engine::guid id) noexcept {
  auto registry = default_registry();
  if (!registry)
    return {};
  return find_type(*registry, id);
}
/** 以下重载使用默认注册表；错误与显式 registry 接口一致，不隐式冻结。 */
[[nodiscard]] inline error register_type(const type_definition& desc) {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return register_type(*registry, desc);
}
[[nodiscard]] inline error register_type(const type_definition& desc,
                                         std::span<const property_binding> bindings) {
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
[[nodiscard]] inline result<std::uint32_t> type_count() noexcept {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return type_count(*registry);
}
[[nodiscard]] inline result<meta::type> type_at(std::uint32_t index) noexcept {
  auto registry = default_registry();
  return registry ? type_at(*registry, index) : result<meta::type>{registry.error()};
}
[[nodiscard]] inline meta::property find_property(guid type, property_id property) noexcept {
  auto registry = default_registry();
  if (!registry)
    return {};
  return find_property(*registry, type, property);
}
[[nodiscard]] inline result<value> read_property(guid type, property_id property,
                                                 const void* object) {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return read_property(*registry, type, property, object);
}
[[nodiscard]] inline error write_property(guid type, property_id property, void* object,
                                          const value& input) {
  auto registry = default_registry();
  if (!registry)
    return registry.error();
  return write_property(*registry, type, property, object, input);
}
} // namespace gneiss::engine::meta
