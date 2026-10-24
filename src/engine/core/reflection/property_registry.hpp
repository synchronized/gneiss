// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/reflection/reflection_types.hpp"
#include "engine/core/result.hpp"
#include <memory>
#include <string>

namespace gneiss::engine {
class type_registry_state;
/** 原生注册表共享所有者；最后一个引用释放时销毁，借用元数据不得超过该生命周期。 */
using registry_ref = std::shared_ptr<type_registry_state>;
/** 内部属性快照拥有文本，复制后可独立用于异步消息或撤销历史。 */
struct property_value {
  std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
               engine::type_guid, property_vec3, property_quaternion>
      payload;
  [[nodiscard]] property_kind kind() const noexcept {
    return payload.valueless_by_exception() ? property_kind::invalid
                                            : static_cast<property_kind>(payload.index());
  }
};
/** 冻结后返回类型数量；失败保持输出。 */
[[nodiscard]] GNEISS_API result type_count(const registry_ref& registry,
                                           std::uint32_t& output) noexcept;
/** 按稳定 GUID 字节顺序枚举冻结元数据，名称借用 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API result describe_type_at(const registry_ref& registry, std::uint32_t index,
                                                 type_info& output) noexcept;
/** 元数据名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::result describe_type(const registry_ref& registry,
                                                              gneiss::engine::type_guid type,
                                                              type_info& output) noexcept;
/** 字段名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::result describe_field(const registry_ref& registry,
                                                               gneiss::engine::type_guid type,
                                                               field_id field,
                                                               field_info& output) noexcept;
/** 读取自有快照；失败保持输出。冻结后的 Registry 可共享，目标线程规则由访问器校验。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
read_property(const registry_ref& registry, gneiss::engine::type_guid type, field_id field,
              property_target target, property_value& output) noexcept;
/** 输入在调用期间借用；沿用访问器的校验、线程与错误语义，不跨线程投递。 */
[[nodiscard]] GNEISS_API gneiss::engine::result
write_property(const registry_ref& registry, gneiss::engine::type_guid type, field_id field,
               property_target target, const property_value& value) noexcept;
} // namespace gneiss::engine
