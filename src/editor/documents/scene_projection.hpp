// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "documents/scene_document_snapshot.hpp"
#include "engine/function/scene/scene_instances.hpp"
namespace gneiss::editor {
/** 将目标文档投影到场景；baseline 仅可传入已成功同步的版本。
 * 对象集合和资源身份不变时，变换、组件及显式层级差异原地更新。
 * 失败不表示目标已同步。
 */
[[nodiscard]] engine::error project_scene_document(engine::world&, engine::scene_instances&,
                                                   scene_instance_id,
                                                   const scene_document_snapshot* baseline,
                                                   const scene_document_snapshot& target) noexcept;
}
