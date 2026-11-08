// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document_snapshot.hpp"
#include "engine/function/scene/scene_instances.hpp"
namespace gneiss::editor {
/** 将目标文档投影到场景；baseline 仅可传入已成功同步的版本。
 * 当前只对纯变换差异原地更新，其他变化完整重建；失败不表示目标已同步。
 */
[[nodiscard]] engine::error project_scene_document(engine::scene_instances&, scene_instance_id,
                                                   const scene_document_snapshot* baseline,
                                                   const scene_document_snapshot& target) noexcept;
}
