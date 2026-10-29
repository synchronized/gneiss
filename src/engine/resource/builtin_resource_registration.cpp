// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "builtin_resource_registration.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/resource/input/action_map_codec.hpp"
#include "engine/resource/material/material_codec.hpp"
#include "engine/resource/scene/prefab_codec.hpp"
#include "engine/resource/scene/scene_codec.hpp"
namespace gneiss::engine {
error register_scene_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<scene_description>{registry}
      .property<&scene_description::uuid>(meta::property_id{1}, "scene_uuid",
                                          meta::property_flags::required)
      .property<&scene_description::objects>(meta::property_id{2}, "objects",
                                             meta::property_flags::required)
      .serialization()
      .serialization<&encode_scene, &decode_scene>()
      .commit();
}
error register_prefab_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<prefab_description>{registry}
      .property<&prefab_description::uuid>(meta::property_id{1}, "prefab_uuid",
                                           meta::property_flags::required)
      .property<&prefab_description::root_uuid>(meta::property_id{2}, "root_uuid",
                                                meta::property_flags::required)
      .property<&prefab_description::objects>(meta::property_id{3}, "objects",
                                              meta::property_flags::required)
      .serialization()
      .serialization<&encode_prefab, &decode_prefab>()
      .commit();
}
error register_action_map_reflection(const meta::registry_ref& registry) noexcept {
  return meta::type_builder<action_map>{registry}
      .serialization<nullptr, &decode_action_map>()
      .commit();
}
}
