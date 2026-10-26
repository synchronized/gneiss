// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/reflection/reflection_types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gneiss::engine {

/** Prefab 实例中节点的稳定作者身份。 */
struct prefab_author_address final {
  std::string instance_uuid;
  std::string source_node_uuid;

  [[nodiscard]] bool operator==(const prefab_author_address&) const noexcept = default;
};

/** 判断 UUID 是否为规范的小写形式。 */
[[nodiscard]] bool is_canonical_prefab_uuid(std::string_view value) noexcept;

/** 判断复合作者身份的两个 UUID 是否均为规范的小写形式。 */
[[nodiscard]] bool is_valid_prefab_author_address(const prefab_author_address& address) noexcept;

using prefab_property_payload =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
                 std::array<std::uint8_t, 16>, std::array<float, 3>, std::array<float, 4>>;

/** 场景作者数据拥有的属性值，不借用 Registry、IPC 或 JSON 内存。 */
struct prefab_property_value final {
  prefab_property_payload payload;

  [[nodiscard]] bool operator==(const prefab_property_value&) const noexcept = default;
};

/** 单个 Prefab 实例字段覆盖的稳定作者键。 */
struct prefab_property_override_key final {
  prefab_author_address node;
  gneiss::engine::guid type_id{};
  gneiss::field_id field_id{};

  [[nodiscard]] bool operator==(const prefab_property_override_key& other) const noexcept;
};

struct prefab_property_override final {
  prefab_property_override_key key;
  prefab_property_value value;
};

/** 返回作者属性值的类别；无值或异常失值返回 invalid。 */
[[nodiscard]] gneiss::engine::value_kind
prefab_property_value_kind(const prefab_property_value& value) noexcept;

/** 按稳定作者键比较，用于确定性排序。 */
[[nodiscard]] bool
prefab_property_override_key_less(const prefab_property_override_key& left,
                                  const prefab_property_override_key& right) noexcept;

} // namespace gneiss::engine
