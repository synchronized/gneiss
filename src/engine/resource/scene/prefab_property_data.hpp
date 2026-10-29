// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/meta/type.hpp"
#include "engine/core/value.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gneiss::engine {

/** Prefab 实例中节点的稳定作者身份。 */
struct prefab_author_address final {
  gneiss::engine::guid instance_uuid;
  gneiss::engine::guid source_node_uuid;

  [[nodiscard]] bool operator==(const prefab_author_address&) const noexcept = default;
};

/** 判断 UUID 是否为规范的小写形式。 */
[[nodiscard]] bool is_canonical_prefab_uuid(std::string_view value) noexcept;

/** 判断复合作者身份的两个 GUID 是否均为非零值。 */
[[nodiscard]] bool is_valid_prefab_author_address(const prefab_author_address& address) noexcept;

/** 单个 Prefab 实例字段覆盖的稳定作者键。 */
struct prefab_property_override_key final {
  gneiss::engine::guid source_node_uuid;
  guid type_guid{};
  meta::property_id property_id{};

  [[nodiscard]] bool operator==(const prefab_property_override_key& other) const noexcept;
};

struct prefab_property_override final {
  prefab_property_override_key key;
  engine::value value;
};

/** 按稳定作者键比较，用于确定性排序。 */
[[nodiscard]] bool
prefab_property_override_key_less(const prefab_property_override_key& left,
                                  const prefab_property_override_key& right) noexcept;

} // namespace gneiss::engine
