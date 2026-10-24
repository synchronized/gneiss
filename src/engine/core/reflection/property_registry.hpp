// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include <gneiss/engine/core/reflection_types.hpp>
#include <memory>
#include <string>

namespace gneiss::engine::core::reflection {
class type_registry_state;
/** 原生所有者或当前操作租约；销毁句柄后不得将租约用作新的公共句柄。 */
using registry_ref = std::shared_ptr<type_registry_state>;
/** 内部属性快照拥有文本，复制后可独立用于异步消息或撤销历史。 */
struct property_value {
  std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
               type_id, property_vec3, property_quaternion>
      payload;
  [[nodiscard]] property_kind kind() const noexcept {
    return payload.valueless_by_exception() ? property_kind::invalid
                                            : static_cast<property_kind>(payload.index());
  }
};
/** 元数据名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::core::result
describe_type(const registry_ref& registry, type_id type, type_info& output) noexcept;
/** 字段名称借用已冻结 Registry；失败保持输出。 */
[[nodiscard]] GNEISS_API gneiss::engine::core::result describe_field(const registry_ref& registry,
                                                                     type_id type, field_id field,
                                                                     field_info& output) noexcept;
/** 读取自有快照；失败保持输出。冻结后的 Registry 可共享，目标线程规则由访问器校验。 */
[[nodiscard]] GNEISS_API gneiss::engine::core::result
read_property(const registry_ref& registry, type_id type, field_id field, property_target target,
              property_value& output) noexcept;
/** 输入在调用期间借用；沿用访问器的校验、线程与错误语义，不跨线程投递。 */
[[nodiscard]] GNEISS_API gneiss::engine::core::result
write_property(const registry_ref& registry, type_id type, field_id field, property_target target,
               const property_value& value) noexcept;
} // namespace gneiss::engine::core::reflection
