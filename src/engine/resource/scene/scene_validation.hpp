// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/diagnostic.hpp"
#include "engine/resource/scene/prefab_description.hpp"
namespace gneiss::engine {
GNEISS_API serialization::diagnostic
validate_property_override_key(const prefab_property_override_key&);
GNEISS_API serialization::diagnostic validate_component_override(const prefab_component_override&);
/** 描述与覆盖的领域校验，供文件、文档恢复及编辑采用复用。 */
GNEISS_API serialization::diagnostic
validate_prefab_reference_description(const prefab_reference_description&);
GNEISS_API serialization::diagnostic
validate_game_object_description(const game_object_description&);
GNEISS_API serialization::diagnostic
validate_mesh_renderer_description(const mesh_renderer_description&);
/** 文件与文档恢复边界递归校验对象和 Prefab 引用，再检查整体层级；组件校验仍待迁移。 */
GNEISS_API serialization::diagnostic validate_scene_description(const scene_description&);
GNEISS_API serialization::diagnostic validate_prefab_description(const prefab_description&);
}
