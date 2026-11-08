// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document_snapshot.hpp"
#include "engine/core/value_edit.hpp"
namespace gneiss::editor {
/** 对象插入、删除及顺序变化保留对象记录。 */
struct document_object_change {
  engine::guid uuid;
  std::optional<engine::object_record> before, after;
  std::size_t before_index{}, after_index{};
};
template <class T> struct document_field_change {
  T before, after;
};
struct document_value_change {
  engine::meta::property_path path;
  engine::value before, after;
};
struct document_component_update {
  engine::guid uuid, type;
  std::vector<document_value_change> properties;
};
struct document_component_change {
  engine::guid uuid;
  std::optional<engine::component_record> before, after;
  std::size_t before_index{}, after_index{};
};
struct document_component_changes {
  std::vector<document_component_change> objects;
  std::vector<document_component_update> updates;
};
/** 存活对象仅保留发生变化的领域字段，不携带其他组件和 Prefab 数据。 */
struct document_object_update {
  engine::guid uuid;
  std::optional<document_field_change<engine::guid>> parent;
  std::optional<document_field_change<std::string>> name;
  std::optional<document_field_change<engine::math::transform>> transform;
  std::optional<document_component_changes> components;
  std::optional<document_field_change<std::optional<engine::prefab_reference_record>>> prefab;
};
template <class T> struct document_map_change {
  std::string key;
  std::optional<T> before, after;
};
struct document_patch {
  engine::guid document;
  std::string uri;
  std::vector<document_object_change> objects;
  std::vector<document_object_update> updates;
  std::vector<document_map_change<prefab_document_snapshot>> prefabs;
  std::vector<document_map_change<engine::render_resource_ref>> assets;
  engine::ref<const engine::scene> before_source, after_source;
  bool source_changed{};
};
[[nodiscard]] engine::result<document_patch>
make_document_patch(const scene_document_snapshot&, const scene_document_snapshot&) noexcept;
/** 合并连续差异；保留最初旧值与最终新值，拒绝不连续的同项变更。 */
[[nodiscard]] engine::result<document_patch> merge_document_patches(const document_patch&,
                                                                    const document_patch&) noexcept;
/** 校验旧值后在独立候选应用；失败不修改输入，不访问 World。 */
[[nodiscard]] engine::result<scene_document_snapshot>
apply_document_patch(const scene_document_snapshot&, const document_patch&, bool undo) noexcept;
}
