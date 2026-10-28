// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "commands/author_document_change.hpp"
#include "documents/scene_document.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
namespace gneiss::editor {
/** 作者历史持有的内存描述和资源引用；不保存节点地址，不读取文件。 */
struct scene_document_snapshot {
  std::string uri;
  engine::prepared_scene_description description;
  engine::scene_instances::asset_bindings assets;
};
[[nodiscard]] engine::error capture_scene_document(runtime::application&, const scene_document&,
                                                   scene_document_snapshot&) noexcept;
/** 仅应用内存中的作者文档变更；依赖必须来自原快照，新增外部资源须先异步加载。 */
[[nodiscard]] engine::error prepare_scene_document(const scene_document_snapshot&,
                                                   std::span<const author_document_change>,
                                                   scene_document_snapshot&) noexcept;
[[nodiscard]] engine::error restore_scene_document(runtime::application&, scene_document&,
                                                   const scene_document_snapshot&) noexcept;
}
