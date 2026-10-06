// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#define GNEISS_REFLECTION_TYPE_REGISTRY_HPP_
#include <algorithm>
#include <cstdint>
#include <gneiss/reflection.h>
#include <memory>
#include <span>
#include <string_view>

namespace gneiss::reflection_internal {
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
};
class type_registry_state;
/** 仅保护当前操作；销毁句柄后不得将此内部租约用作新的公共句柄。 */
using registry_ref = std::shared_ptr<type_registry_state>;
[[nodiscard]] inline bool valid_type_id(gneiss_type_id id) noexcept {
  return std::ranges::any_of(id.bytes, [](std::uint8_t byte) { return byte != 0U; });
}
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
} // namespace gneiss::reflection_internal
#endif
