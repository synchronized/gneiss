// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/diagnostic.hpp"
#include "engine/resource/scene/prefab_description.hpp"
namespace gneiss {
/** 组件领域规则，由场景文件、文档采用及组件适配器显式调用；通用序列化不执行。 */
struct component_validation_policy {
  serialization::diagnostic (*validate)(const component_description_base&){};
};
template <class T, auto Validate>
inline constexpr component_validation_policy component_validation{
    [](const component_description_base& description) -> serialization::diagnostic {
      const auto* typed = description.template get<T>();
      return typed ? std::invoke(Validate, *typed)
                   : serialization::diagnostic{.error = error::invalid_argument};
    }};
GNEISS_API serialization::diagnostic validate_camera_description(const camera_description&);
GNEISS_API serialization::diagnostic
validate_property_override_key(const prefab_property_override_key&);
GNEISS_API serialization::diagnostic validate_component_override(const prefab_component_override&);
/** 描述与覆盖的领域校验，供文件、文档恢复及编辑采用复用。 */
GNEISS_API serialization::diagnostic
validate_prefab_reference_description(const prefab_reference_description&);
GNEISS_API serialization::diagnostic
validate_game_object_description(const game_object_description&);
GNEISS_API serialization::diagnostic
validate_mesh_component_description(const mesh_component_description&);
/** 文件与文档恢复边界递归校验对象和 Prefab 引用，再检查整体层级及组件领域规则。 */
GNEISS_API serialization::diagnostic validate_scene_description(const scene_description&);
GNEISS_API serialization::diagnostic validate_prefab_description(const prefab_description&);
}
