// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <array>
#include <cstdint>
#include <thread>
#include <type_traits>

static_assert(!std::is_convertible_v<gneiss::engine::rid, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::scene_instance_id, gneiss::engine::rid>);
static_assert(!std::is_convertible_v<gneiss::scene_node_id, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::prefab_refresh_token, gneiss::scene_node_id>);

namespace {
bool native_operations() {
  using namespace gneiss;
  gneiss::engine::world_owner owner;
  if (gneiss::engine::create_native(owner) != gneiss::engine::result::success)
    return false;
  entity_id entity{};
  scene_node_id node{};
  bool alive{};
  std::uint64_t count{};
  if (owner->entity_create(entity) != gneiss::engine::result::success ||
      owner->node_create(gneiss::scene_node_id{0}, gneiss::entity_id{entity}, node) !=
          gneiss::engine::result::success ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) != gneiss::engine::result::success ||
      !alive || owner->entity_count(count) != gneiss::engine::result::success || count != 1)
    return false;
  gneiss::engine::transform local;
  local.translation = {2, 3, 4};
  gneiss::engine::transform actual;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, local) !=
          gneiss::engine::result::success ||
      owner->node_get_world_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::result::success ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, invalid) !=
          gneiss::engine::result::invalid_argument ||
      owner->node_get_local_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::result::success ||
      actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (owner->node_get_local_transform(gneiss::scene_node_id{0}, actual) !=
          gneiss::engine::result::invalid_handle ||
      actual.translation[0] != 42)
    return false;
  gneiss::engine::result foreign{};
  std::thread worker([&] { foreign = owner->entity_destroy(gneiss::entity_id{entity}); });
  worker.join();
  if (foreign != gneiss::engine::result::invalid_state ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) != gneiss::engine::result::success ||
      !alive)
    return false;
  owner.reset();
  return !owner;
}

}

int main() {
  if (!native_operations())
    return 20;
  namespace service = gneiss::engine;
  gneiss::engine::world_owner world{};
  if (gneiss::engine::create_native(world) != gneiss::engine::result::success) {
    return 1;
  }
  std::uint64_t entity{};
  if (gneiss::engine::write_identifier<gneiss::entity_id>(entity, [&](auto& native_output) {
        return world->entity_create(native_output);
      }) != gneiss::engine::result::success) {
    return 2;
  }
  std::uint64_t count{};
  if (world->entity_count(count) != gneiss::engine::result::success || count != 1U) {
    return 3;
  }
  gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (world->entity_configure_camera(gneiss::entity_id{entity}, camera) !=
      gneiss::engine::result::success) {
    return 4;
  }
  service::camera_settings settings;
  if (world->entity_get_camera(gneiss::entity_id{entity}, settings) !=
          gneiss::engine::result::success ||
      settings.near != camera.near || settings.far != camera.far) {
    return 5;
  }
  settings.far = settings.near;
  if (world->entity_configure_camera(gneiss::entity_id{entity}, settings) !=
          gneiss::engine::result::invalid_argument ||
      world->entity_get_camera(gneiss::entity_id{entity}, camera) !=
          gneiss::engine::result::success ||
      camera.near == camera.far) {
    return 6;
  }
  gneiss::engine::result wrong_thread{};
  gneiss::engine::registry_ref reflection;
  if (gneiss::engine::create_property_registry(reflection).failed())
    return 14;
  gneiss::engine::transform candidate_transform;
  gneiss::engine::value proposed;
  proposed.payload = gneiss::property_vec3{2, 3, 4};
  if (gneiss::engine::set_property(reflection, service::transform_property_type(),
                                   gneiss::engine::transform_translation_field.get(),
                                   &candidate_transform,
                                   proposed) != gneiss::engine::result::success ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload = gneiss::property_vec3{0, 1, 1};
  if (gneiss::engine::set_property(reflection, service::transform_property_type(),
                                   gneiss::engine::transform_scale_field.get(),
                                   &candidate_transform,
                                   proposed) != gneiss::engine::result::invalid_argument ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_properties candidate_camera{{1, 10, 20}, false};
  proposed.payload = 30.0F;
  if (gneiss::engine::set_property(reflection, service::camera_property_type(),
                                   gneiss::engine::camera_near_plane_field.get(), &candidate_camera,
                                   proposed) != gneiss::engine::result::success) {
    return 17;
  }
  proposed.payload = 40.0F;
  if (gneiss::engine::set_property(reflection, service::camera_property_type(),
                                   gneiss::engine::camera_far_plane_field.get(), &candidate_camera,
                                   proposed) != gneiss::engine::result::success ||
      candidate_camera.settings.near != 30 || candidate_camera.settings.far != 40) {
    return 18;
  }
  // 借用只在拥有者存活期使用；移动拥有者不改变对象地址。
  service::world_owner native;
  if (service::create_native(native).failed())
    return 14;
  const auto* original = native.get();
  if (service::create_native(native) != service::result::invalid_state || native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (native || moved.get() != original || moved->entity_count(count).failed())
    return 16;
  std::thread worker([&] { wrong_thread = moved->entity_count(count); });
  worker.join();
  if (wrong_thread != service::result::invalid_state)
    return 17;
  moved.reset();
  return 0;
}
