// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_H_
#define GNEISS_SCENE_PREFAB_PROPERTY_OVERRIDE_H_

#include <gneiss/reflection.h>

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::scene_internal {

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
  gneiss_type_id type_id{};
  gneiss_field_id field_id = GNEISS_NULL_FIELD_ID;

  [[nodiscard]] bool operator==(const prefab_property_override_key& other) const noexcept;
};

struct prefab_property_override final {
  prefab_property_override_key key;
  prefab_property_value value;
};

/** 返回 owning 值对应的公共属性类别；无值返回 INVALID。 */
[[nodiscard]] gneiss_property_kind
prefab_property_value_kind(const prefab_property_value& value) noexcept;

/** 验证作者键、值、字段存在性、类型和可写能力。Registry 必须已经冻结。 */
[[nodiscard]] gneiss_result
validate_prefab_property_override(gneiss_type_registry registry,
                                  const prefab_property_override& value) noexcept;

/**
 * 写入或移除一条稀疏覆盖。
 *
 * candidate 与 source_value 规范化后相等时删除对应记录，否则按稳定键更新并确定性排序。
 */
[[nodiscard]] gneiss_result set_prefab_property_override(
    gneiss_type_registry registry, std::vector<prefab_property_override>& overrides,
    prefab_property_override candidate, prefab_property_value source_value) noexcept;

/** 按稳定作者键比较，用于确定性排序。 */
[[nodiscard]] bool
prefab_property_override_key_less(const prefab_property_override_key& left,
                                  const prefab_property_override_key& right) noexcept;

} // namespace gneiss::scene_internal

#endif
