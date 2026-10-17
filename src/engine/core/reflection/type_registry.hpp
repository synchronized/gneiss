// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#define GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#include "property_registry.hpp"
#include <algorithm>
#include <cstdint>
#include <gneiss/engine/reflection.h>
#include <memory>
#include <span>
#include <string_view>

namespace gneiss::reflection_internal {
/** 借用候选数据，仅修改该数据并校验；禁止场景副作用和普通业务通知。 */
using candidate_property_setter = gneiss_result (*)(void* user_data, void* candidate,
                                                    const gneiss_property_value& value);
struct field_definition {
  gneiss_field_id id{};
  gneiss_type_id value_type_id{};
  std::uint32_t flags{};
  std::string_view name;
};
struct type_definition {
  gneiss_type_id id{};
  std::uint32_t schema_version{};
  std::string_view name;
  std::span<const field_definition> fields;
};
/** 回调及上下文均借用；沿用公开回调协议，不隐式持有用户对象。 */
struct property_binding {
  std::uint32_t kind{};
  gneiss_property_getter getter{};
  gneiss_property_setter setter{};
  void* user_data{};
  candidate_property_setter candidate_setter{};
};
[[nodiscard]] inline bool valid_type_id(gneiss_type_id id) noexcept {
  return std::ranges::any_of(id.bytes, [](std::uint8_t byte) { return byte != 0U; });
}
/** 创建同一实现的原生对象，不注册公共 RID；失败保持原输出。 */
[[nodiscard]] GNEISS_API gneiss_result create_native(registry_ref& output) noexcept;
[[nodiscard]] GNEISS_API registry_ref resolve(gneiss_type_registry registry);
[[nodiscard]] GNEISS_API gneiss_result create(gneiss_type_registry& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result destroy(gneiss_type_registry registry) noexcept;
[[nodiscard]] GNEISS_API gneiss_result register_type(const registry_ref& state,
                                                     const type_definition& desc) noexcept;
[[nodiscard]] GNEISS_API gneiss_result bind_property(const registry_ref& state,
                                                     gneiss_type_id type_id,
                                                     gneiss_field_id field_id,
                                                     const property_binding& desc) noexcept;
[[nodiscard]] GNEISS_API gneiss_result freeze(const registry_ref& state) noexcept;
[[nodiscard]] GNEISS_API gneiss_result is_frozen(const registry_ref& state,
                                                 std::uint8_t& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result type_count(const registry_ref& state,
                                                  std::uint32_t& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result type_at(const registry_ref& state, std::uint32_t index,
                                               gneiss_type_info& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result find_type(const registry_ref& state, gneiss_type_id id,
                                                 gneiss_type_info& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result find_field(const registry_ref& state, gneiss_type_id type_id,
                                                  gneiss_field_id field_id,
                                                  gneiss_field_info& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result get_property(const registry_ref& state,
                                                    gneiss_type_id type_id,
                                                    gneiss_field_id field_id,
                                                    gneiss_property_target target,
                                                    gneiss_property_value& output) noexcept;
[[nodiscard]] GNEISS_API gneiss_result set_property(const registry_ref& state,
                                                    gneiss_type_id type_id,
                                                    gneiss_field_id field_id,
                                                    gneiss_property_target target,
                                                    const gneiss_property_value& value) noexcept;
/** 不回退到运行时 setter。candidate 为字段所属类型的可写候选数据，调用期间借用。
 * 调用方独占候选，失败后丢弃整份候选；异常转换为 INTERNAL，不发送通知。
 */
[[nodiscard]] GNEISS_API gneiss_result
set_candidate_property(const registry_ref& state, gneiss_type_id type_id, gneiss_field_id field_id,
                       void* candidate, const gneiss_property_value& value) noexcept;
} // namespace gneiss::reflection_internal
#endif
