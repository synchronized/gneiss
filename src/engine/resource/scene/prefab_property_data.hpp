// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/meta/property_path.hpp"
#include "engine/core/meta/type.hpp"
#include "engine/core/value_edit.hpp"
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

/** 先定位来源对象，再选择该对象上的属性所属类型。 */
struct prefab_property_target final {
  static constexpr std::string_view type_name = "prefab_property_target";
  static constexpr guid type_guid = guid::literal("dfdb1dac-6d93-4326-8909-70735ee03e7f");
  guid source_node_uuid;
  guid target_type_guid;
  guid component_uuid{}; // Transform 为 nil；组件必须使用具体实例身份。
  bool operator==(const prefab_property_target&) const noexcept = default;
};

/** 完整覆盖地址；path 从目标根开始，不持有对象或容器地址。 */
struct prefab_property_override_key final {
  static constexpr std::string_view type_name = "prefab_property_override_key";
  static constexpr guid type_guid = guid::literal("9c2683a8-3bfb-48b4-92af-6bce11ab215e");
  prefab_property_target target;
  meta::property_path path;
  [[nodiscard]] meta::property_id first_property() const noexcept {
    if (!path.empty())
      if (const auto* id = std::get_if<meta::property_id>(&path.front()))
        return *id;
    return {};
  }
  bool operator==(const prefab_property_override_key&) const noexcept = default;
};

struct prefab_property_override final {
  static constexpr std::string_view type_name = "prefab_property_override";
  static constexpr guid type_guid = guid::literal("8a714394-5d76-4aab-9787-46fb7cb53025");
  prefab_property_override_key key;
  value_operation operation;
  /** 只用于已确认的 set 操作；集合操作通过 operation 访问。 */
  engine::value& replacement() { return std::get<set_value_operation>(operation).content; }
  const engine::value& replacement() const {
    return std::get<set_value_operation>(operation).content;
  }
};

/** 按稳定作者键比较，用于确定性排序。 */
[[nodiscard]] bool
prefab_property_override_key_less(const prefab_property_override_key& left,
                                  const prefab_property_override_key& right) noexcept;

} // namespace gneiss::engine
