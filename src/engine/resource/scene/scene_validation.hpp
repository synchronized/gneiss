// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/resource/scene/prefab_description.hpp"
namespace gneiss::engine {
meta::serialization_diagnostic validate_property_override_key(const prefab_property_override_key&);
meta::serialization_diagnostic validate_component_override(const prefab_component_override&);
/** 局部描述校验，供反射注册使用。 */
meta::serialization_diagnostic
validate_prefab_reference_description(const prefab_reference_description&);
meta::serialization_diagnostic validate_object_description(const object_description&);
meta::serialization_diagnostic validate_camera_description(const math::camera_settings&);
meta::serialization_diagnostic validate_mesh_renderer_description(const mesh_renderer_description&);
/** 仅检查描述的整体约束，局部字段由各自反射校验器负责。 */
meta::serialization_diagnostic validate_scene_description(const scene_description&);
meta::serialization_diagnostic validate_prefab_description(const prefab_description&);
}
