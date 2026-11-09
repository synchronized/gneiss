// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/policy.hpp"
#include "engine/resource/scene/prefab_description.hpp"
namespace gneiss::engine {
serialization::diagnostic validate_property_override_key(const prefab_property_override_key&);
serialization::diagnostic validate_component_override(const prefab_component_override&);
/** 局部描述校验，供反射注册使用。 */
serialization::diagnostic
validate_prefab_reference_description(const prefab_reference_description&);
serialization::diagnostic validate_game_object_description(const game_object_description&);
serialization::diagnostic validate_mesh_renderer_description(const mesh_renderer_description&);
/** 文件与文档恢复边界调用整体约束；局部字段校验仍待从反射迁移。 */
serialization::diagnostic validate_scene_description(const scene_description&);
serialization::diagnostic validate_prefab_description(const prefab_description&);
}
