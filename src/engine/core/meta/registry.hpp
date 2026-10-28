// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/meta/type.hpp"
#include "engine/core/type_id.hpp"
#include <memory>

namespace gneiss::engine::meta {
class type_registry_state;
/** 原生注册表共享所有者；最后一个引用释放时销毁，借用元数据不得超过该生命周期。 */
using registry_ref = std::shared_ptr<type_registry_state>;
/** 创建注册表共享所有者；失败保持原输出。 */
[[nodiscard]] GNEISS_API error create_registry(registry_ref& output) noexcept;
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
[[nodiscard]] GNEISS_API error is_frozen(const registry_ref& state, bool& output) noexcept;
/** 冻结后查询函数表；失败保持输出，未登记能力返回 unsupported。 */
[[nodiscard]] GNEISS_API error find_serialization(const registry_ref&, type_id,
                                                  serialization_binding&) noexcept;
/** 冻结后返回类型数量；失败保持输出。 */
[[nodiscard]] GNEISS_API error type_count(const registry_ref& registry,
                                          std::uint32_t& output) noexcept;
/** 按稳定 GUID 字节顺序枚举冻结元数据，名称借用 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API error describe_type_at(const registry_ref& registry, std::uint32_t index,
                                                type_info& output) noexcept;
/** 元数据名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API error describe_type(const registry_ref& registry, engine::guid type,
                                             type_info& output) noexcept;
/** 按当前程序的 C++ 类型身份查询同一份冻结元数据；零或未知 ID 返回 not_found。 */
[[nodiscard]] GNEISS_API error describe_type(const registry_ref& registry, engine::type_id type,
                                             type_info& output) noexcept;
template <class T>
[[nodiscard]] error describe_type(const registry_ref& registry, type_info& output) noexcept {
  return describe_type(registry, runtime_type_of<T>(), output);
}
/** 字段名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API error describe_property(const registry_ref& registry, engine::guid type,
                                                 property_id property,
                                                 property_info& output) noexcept;
/** 读取自有快照；失败保持输出。冻结后的 Registry 可共享，对象的类型、寿命和线程访问由调用方保证。
 */
[[nodiscard]] GNEISS_API error read_property(const registry_ref& registry, engine::guid type,
                                             property_id property, const void* object,
                                             value& output) noexcept;
/** 输入在调用期间借用；沿用访问器的校验、线程与错误语义，不跨线程投递。 */
[[nodiscard]] GNEISS_API error write_property(const registry_ref& registry, engine::guid type,
                                              property_id property, void* object,
                                              const value& value) noexcept;
} // namespace gneiss::engine::meta
