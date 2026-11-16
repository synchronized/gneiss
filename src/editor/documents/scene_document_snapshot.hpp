// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/author_document_change.hpp"
#include <gneiss/engine/function/scene/composition.hpp>
#include <gneiss/engine/resource/prefab.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <gneiss/engine/resource/scene/scene_record.hpp>
#include "mesh_binding.hpp"
#include <map>
#include <span>
#include <string>
namespace gneiss::editor {
/** 子树历史保活的实际资源；按文档对象 UUID 关联，不保存运行节点。 */
struct scene_object_resources {
  scene_mesh_binding renderer;
  gneiss::prefab_record prefab_content;
  [[nodiscard]] gneiss::result<gneiss::prefab_asset_ref> prefab_source() const;
  std::vector<scene_mesh_binding> source_nodes;
  gneiss::ref<const gneiss::prefab> source_asset;
};
/** 文档子树快照：只冻结描述和值，不捕获 ECS 或运行对象身份。 */
struct scene_subtree_snapshot {
  gneiss::scene_record content;
  [[nodiscard]] gneiss::result<gneiss::scene_description> description() const;
  gneiss::guid root_uuid;
  gneiss::object_address parent;
  std::map<gneiss::guid, scene_object_resources> resources;
};
/** 捕获的对象描述和固定 Prefab 来源；不读取资产当前修订。 */
struct prefab_header_snapshot {
  gneiss::game_object_description author;
  gneiss::prefab_asset_ref source;
  gneiss::ref<const gneiss::prefab> source_asset;
};
/** 冻结 Prefab 文档快照；只使用显式输入资源，不访问 World、文件或资产缓存。 */
[[nodiscard]] gneiss::result<scene_subtree_snapshot> freeze_prefab_snapshot(
    prefab_header_snapshot source,
    const std::map<std::string, gneiss::render_resource_ref, std::less<>>& assets);

/** 历史冻结来源内容；资产强引用仅负责保活，不用于重新获取当前修订。 */
struct prefab_document_snapshot {
  gneiss::prefab_record content;
  gneiss::ref<const gneiss::prefab> source_asset;
  bool operator==(const prefab_document_snapshot& other) const { return content == other.content; }
};
/** 作者历史持有不可变值树和资源引用；不保存节点地址，不读取文件。 */
struct scene_document_snapshot {
  std::string uri;
  gneiss::scene_record content;
  std::map<std::string, prefab_document_snapshot> prefabs;
  gneiss::ref<const gneiss::scene> source_asset;
  [[nodiscard]] gneiss::result<gneiss::scene_description> description() const;
  std::map<std::string, gneiss::render_resource_ref, std::less<>> assets;
  /** 只检查当前版本和冻结来源；外部文档未提供时保留未解析诊断。 */
  [[nodiscard]] gneiss::result<std::vector<gneiss::object_reference_issue>>
  reference_issues() const;
};
/** 从文档及冻结来源生成有效对象；保留覆盖冲突，不读取文件或运行实例。 */
[[nodiscard]] gneiss::result<gneiss::composed_scene>
compose_scene_document(const scene_document_snapshot&);
struct prepared_scene_document {
  scene_document_snapshot snapshot;
  std::vector<gneiss::render_resource_source> missing;
};
/** 只计算内存变更及缺失依赖；missing 表示尚未持有资源身份，不要求内容加载完成。 */
[[nodiscard]] gneiss::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot&, std::span<const author_document_change>);
/** 使用加载回执冻结的内容构造文档，不读取 World 或当前资产修订。 */
[[nodiscard]] gneiss::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const gneiss::scene_content&,
                             gneiss::ref<const gneiss::scene> source = {});
/** 恢复独立的领域输入；资源引用只保活，内容使用冻结值。 */
[[nodiscard]] gneiss::result<gneiss::scene_content>
make_scene_document_inputs(const scene_document_snapshot&);
}
