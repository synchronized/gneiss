// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>
#include <gneiss/engine/core/data/value_edit.hpp>
#include <gneiss/engine/core/meta/property_path.hpp>
#include <gneiss/engine/core/meta/type.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gneiss {

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
  gneiss::value& replacement() { return std::get<set_value_operation>(operation).content; }
  const gneiss::value& replacement() const {
    return std::get<set_value_operation>(operation).content;
  }
};

/** 按稳定作者键比较，用于确定性排序。 */
[[nodiscard]] bool
prefab_property_override_key_less(const prefab_property_override_key& left,
                                  const prefab_property_override_key& right) noexcept;

} // namespace gneiss
