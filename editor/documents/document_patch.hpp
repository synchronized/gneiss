// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document_snapshot.hpp"
#include <gneiss/engine/core/data/value_edit.hpp>
namespace gneiss::editor {
/** 对象插入、删除及顺序变化保留对象记录。 */
struct document_object_change {
  gneiss::guid uuid;
  std::optional<gneiss::object_record> before, after;
  std::size_t before_index{}, after_index{};
};
template <class T> struct document_field_change {
  T before, after;
};
struct document_value_change {
  gneiss::property_path path;
  gneiss::value before, after;
};
struct document_component_update {
  gneiss::guid uuid, type;
  std::vector<document_value_change> properties;
};
struct document_component_change {
  gneiss::guid uuid;
  std::optional<gneiss::component_record> before, after;
  std::size_t before_index{}, after_index{};
};
struct document_component_changes {
  std::vector<document_component_change> objects;
  std::vector<document_component_update> updates;
};
struct document_override_change {
  gneiss::prefab_property_override_key key;
  std::size_t occurrence{};
  std::optional<gneiss::value_operation> before, after;
};
struct document_prefab_change {
  // 引用增加、移除或更换来源时才保存完整引用。
  std::optional<document_field_change<std::optional<gneiss::prefab_reference_record>>> replacement;
  std::string uri;
  std::vector<gneiss::prefab_property_override_key> before_order, after_order;
  std::vector<document_override_change> overrides;
  std::optional<document_field_change<std::vector<gneiss::component_override_record>>> components;
};
/** 存活对象仅保留发生变化的领域字段，不携带其他组件和 Prefab 数据。 */
struct document_object_update {
  gneiss::guid uuid;
  std::optional<document_field_change<gneiss::object_address>> parent;
  std::optional<document_field_change<std::string>> name;
  std::optional<document_field_change<gneiss::math::transform>> transform;
  std::optional<document_component_changes> components;
  std::optional<document_prefab_change> prefab;
};
template <class T> struct document_map_change {
  std::string key;
  std::optional<T> before, after;
};
struct document_patch {
  gneiss::guid document;
  std::string uri;
  std::vector<document_object_change> objects;
  std::vector<document_object_update> updates;
  std::vector<document_map_change<prefab_document_snapshot>> prefabs;
  std::vector<document_map_change<gneiss::render_resource_ref>> assets;
  gneiss::ref<const gneiss::scene> before_source, after_source;
  bool source_changed{};
};
[[nodiscard]] gneiss::result<document_patch> make_document_patch(const scene_document_snapshot&,
                                                                 const scene_document_snapshot&);
/** 合并连续差异；保留最初旧值与最终新值，拒绝不连续的同项变更。 */
[[nodiscard]] gneiss::result<document_patch> merge_document_patches(const document_patch&,
                                                                    const document_patch&);
/** 校验旧值后在独立候选应用；失败不修改输入，不访问 World。 */
[[nodiscard]] gneiss::result<scene_document_snapshot>
apply_document_patch(const scene_document_snapshot&, const document_patch&, bool undo);
}
