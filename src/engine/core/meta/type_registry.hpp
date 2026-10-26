// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/type_info.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include <memory>
#include <string>

namespace gneiss::engine {
class type_registry_state;
/** 原生注册表共享所有者；最后一个引用释放时销毁，借用元数据不得超过该生命周期。 */
using registry_ref = std::shared_ptr<type_registry_state>;
/** 冻结后返回类型数量；失败保持输出。 */
[[nodiscard]] GNEISS_API result type_count(const registry_ref& registry,
                                           std::uint32_t& output) noexcept;
/** 按稳定 GUID 字节顺序枚举冻结元数据，名称借用 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API result describe_type_at(const registry_ref& registry, std::uint32_t index,
                                                 type_info& output) noexcept;
/** 元数据名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
describe_type(const registry_ref& registry, gneiss::engine::guid type, type_info& output) noexcept;
/** 按当前程序的 C++ 类型身份查询同一份冻结元数据；零或未知 ID 返回 not_found。 */
[[nodiscard]] GNEISS_API result describe_type(const registry_ref& registry, engine::type_id type,
                                              type_info& output) noexcept;
template <class T>
[[nodiscard]] result describe_type(const registry_ref& registry, type_info& output) noexcept {
  return describe_type(registry, runtime_type_of<T>(), output);
}
/** 字段名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::result describe_field(const registry_ref& registry,
                                                               gneiss::engine::guid type,
                                                               field_id field,
                                                               field_info& output) noexcept;
/** 读取自有快照；失败保持输出。冻结后的 Registry 可共享，对象的类型、寿命和线程访问由调用方保证。
 */
[[nodiscard]] GNEISS_API gneiss::engine::result
read_property(const registry_ref& registry, gneiss::engine::guid type, field_id field,
              const void* object, gneiss::engine::value& output) noexcept;
/** 输入在调用期间借用；沿用访问器的校验、线程与错误语义，不跨线程投递。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
write_property(const registry_ref& registry, gneiss::engine::guid type, field_id field,
               void* object, const gneiss::engine::value& value) noexcept;
} // namespace gneiss::engine
