// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/author_document_change.hpp"
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/resource/scene/scene_record.hpp"
#include <map>
#include <span>
#include <string>
namespace gneiss::editor {
/** 历史冻结来源内容；资产强引用仅负责保活，不用于重新获取当前修订。 */
struct prefab_document_snapshot {
  engine::prefab_record content;
  engine::ref<const engine::prefab> source_asset;
  bool operator==(const prefab_document_snapshot& other) const { return content == other.content; }
};
/** 作者历史持有不可变值树和资源引用；不保存节点地址，不读取文件。 */
struct scene_document_snapshot {
  std::string uri;
  engine::scene_record content;
  std::map<std::string, prefab_document_snapshot> prefabs;
  engine::ref<const engine::scene> source_asset;
  [[nodiscard]] engine::result<engine::scene_description> description() const noexcept;
  std::map<std::string, engine::render_resource_ref, std::less<>> assets;
  /** 只检查当前版本和冻结来源；外部文档未提供时保留未解析诊断。 */
  [[nodiscard]] engine::result<std::vector<engine::object_reference_issue>>
  reference_issues() const noexcept;
};
/** 文档内地址：普通对象 source 为空，Prefab 成员由实例 UUID 与来源 UUID 联合定位。 */
struct document_object_address {
  engine::guid object, source;
  auto operator<=>(const document_object_address&) const = default;
};
struct composed_document_object {
  document_object_address address, parent;
  engine::game_object_description effective;
  std::string prefab_uri;
  engine::math::transform source_transform{};
  bool instance_root = false;
  std::vector<engine::prefab_property_override_key> overrides;
};
/** 临时组合结果，不持有 World 句柄，不作为可编辑权威状态。 */
struct composed_scene_document {
  std::vector<composed_document_object> objects;
  std::map<engine::guid, std::vector<engine::prefab_override_conflict>> conflicts;
};
/** 从文档及冻结来源生成有效对象；保留覆盖冲突，不读取文件或运行实例。 */
[[nodiscard]] engine::result<composed_scene_document>
compose_scene_document(const scene_document_snapshot&) noexcept;
struct prepared_scene_document {
  scene_document_snapshot snapshot;
  std::vector<engine::render_resource_source> missing;
};
/** 只计算内存变更及缺失依赖；missing 表示尚未持有资源身份，不要求内容加载完成。 */
[[nodiscard]] engine::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot&,
                       std::span<const author_document_change>) noexcept;
/** 从已准备描述冻结内容；纯内存操作，不访问宿主或 World。 */
[[nodiscard]] engine::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const engine::scene_instantiation_data&) noexcept;
/** 使用加载回执冻结的内容构造文档，不读取 World 或当前资产修订。 */
[[nodiscard]] engine::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const engine::scene_content&,
                             engine::ref<const engine::scene> source) noexcept;
/** 恢复独立的领域输入；资源引用只保活，内容使用冻结值。 */
[[nodiscard]] engine::result<engine::scene_preparation_inputs>
make_scene_document_inputs(const scene_document_snapshot&) noexcept;
}
