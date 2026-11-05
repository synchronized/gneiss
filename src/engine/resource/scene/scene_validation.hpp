// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/resource/scene/prefab_description.hpp"
namespace gneiss::engine {
/** 仅检查描述的整体约束，局部字段由各自反射校验器负责。 */
meta::serialization_diagnostic validate_scene_description(const scene_description&);
meta::serialization_diagnostic validate_prefab_description(const prefab_description&);
}
