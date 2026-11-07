// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/author_document_change.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include <map>
#include <span>
#include <string>
namespace gneiss::editor {
/** 历史冻结来源内容；资产强引用仅负责保活，不用于重新获取当前修订。 */
struct prefab_document_snapshot {
  engine::value content;
  engine::ref<const engine::prefab> source_asset;
  bool operator==(const prefab_document_snapshot& other) const { return content == other.content; }
};
/** 作者历史持有不可变值树和资源引用；不保存节点地址，不读取文件。 */
struct scene_document_snapshot {
  std::string uri;
  engine::value content;
  std::map<std::string, prefab_document_snapshot> prefabs;
  engine::ref<const engine::scene> source_asset;
  [[nodiscard]] engine::result<engine::scene_description> description() const noexcept;
  std::map<std::string, engine::render_resource_ref, std::less<>> assets;
};
struct prepared_scene_document {
  scene_document_snapshot snapshot;
  std::vector<engine::render_resource_source> missing;
};
/** 只计算内存变更及缺失依赖；missing 非空时必须补齐资源后才能采用快照。 */
[[nodiscard]] engine::result<prepared_scene_document>
prepare_scene_document(const scene_document_snapshot&,
                       std::span<const author_document_change>) noexcept;
/** 从已准备描述冻结内容；纯内存操作，不访问宿主或 World。 */
[[nodiscard]] engine::result<scene_document_snapshot>
make_scene_document_snapshot(std::string uri, const engine::scene_instantiation_data&) noexcept;
/** 恢复独立的领域输入；资源引用只保活，内容使用冻结值。 */
[[nodiscard]] engine::result<engine::scene_preparation_inputs>
make_scene_document_inputs(const scene_document_snapshot&) noexcept;
}
