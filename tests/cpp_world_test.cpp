// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <thread>
#include <type_traits>
#include <utility>

static_assert(!std::is_same_v<gneiss::camera_desc, gneiss_camera_desc>);
static_assert(gneiss::camera_desc{}.near_plane == 0.1F);
static_assert(gneiss::camera{}.is_primary);
static_assert(!gneiss::mesh_renderer{}.mesh.is_valid());

namespace {
bool verify_camera(gneiss::world& world, gneiss::entity_id entity) {
  gneiss::camera_desc expected{
      .vertical_field_of_view_radians = 0.8F,
      .near_plane = 0.5F,
      .far_plane = 42.0F,
  };
  gneiss::camera_desc output{};
  if (world.configure_camera(entity, expected).failed() ||
      world.get_camera(entity, output).failed() ||
      output.vertical_field_of_view_radians != expected.vertical_field_of_view_radians ||
      output.near_plane != expected.near_plane || output.far_plane != expected.far_plane) {
    return false;
  }
  auto invalid = expected;
  invalid.far_plane = 0.1F;
  if (world.configure_camera(entity, invalid) != gneiss::result::invalid_argument ||
      world.get_camera({}, output) != gneiss::result::invalid_handle || output.far_plane != 42.0F) {
    return false;
  }
  gneiss::result other_thread = gneiss::result::success;
  std::thread worker([&] { other_thread = world.get_camera(entity, output); });
  worker.join();
  if (other_thread != gneiss::result::invalid_state || output.far_plane != 42.0F) {
    return false;
  }
  return world.get_camera(entity, output).ok() && output.far_plane == 42.0F;
}
} // namespace

int main() {
  gneiss::world first;
  if (gneiss::world::create(first) != gneiss::result::success) {
    return 1;
  }
  gneiss::entity_id entity;
  if (first.create_entity(entity) != gneiss::result::success || !entity.is_valid()) {
    return 2;
  }
  bool is_alive = false;
  if (first.is_alive(entity, is_alive) != gneiss::result::success || !is_alive) {
    return 3;
  }
  gneiss::camera_desc camera{};
  gneiss::camera_desc queried_camera{};
  gneiss::entity_id active_camera = entity;
  if (first.get_active_camera(active_camera) != gneiss::result::not_ready ||
      active_camera.is_valid()) {
    return 9;
  }
  if (first.configure_camera(entity, camera) != gneiss::result::success ||
      first.get_camera(entity, queried_camera) != gneiss::result::success ||
      first.set_active_camera(entity) != gneiss::result::success ||
      first.get_active_camera(active_camera) != gneiss::result::success ||
      active_camera != entity) {
    return 4;
  }
  if (first.remove_camera(entity).failed() ||
      first.remove_camera(entity) != gneiss::result::not_found ||
      first.get_active_camera(active_camera) != gneiss::result::not_ready ||
      active_camera.is_valid()) {
    return 10;
  }
  if (!verify_camera(first, entity)) {
    return 12;
  }
  const gneiss::camera legacy_camera{};
  if (first.set_camera(entity, legacy_camera).failed() ||
      first.get_active_camera(active_camera).failed() || active_camera != entity) {
    return 11;
  }

  std::uint64_t count{};
  gneiss::scene_node_id root;
  gneiss::scene_node_id child;
  gneiss::scene_node_id parent;
  gneiss::entity_id associated;
  gneiss::transform local{};
  local.translation[0] = 7.0F;
  gneiss::transform queried{};
  if (first.entity_count(count) != gneiss::result::success || count != 1U ||
      first.create_scene_node({}, {}, root) != gneiss::result::success ||
      first.create_scene_node(root, entity, child) != gneiss::result::success ||
      first.set_local_transform(child, local) != gneiss::result::success ||
      first.get_local_transform(child, queried) != gneiss::result::success ||
      queried.translation[0] != 7.0F ||
      first.get_entity(child, associated) != gneiss::result::success || associated != entity ||
      first.get_parent(child, parent) != gneiss::result::success || parent != root ||
      first.get_parent(root, parent) != gneiss::result::success || parent.is_valid()) {
    return 6;
  }
  gneiss::type_registry registry;
  gneiss_type_info info = GNEISS_TYPE_INFO_INIT;
  if (gneiss::type_registry::create(registry) != gneiss::result::success ||
      gneiss::world::register_reflection(registry) != gneiss::result::success ||
      registry.freeze() != gneiss::result::success ||
      registry.find_type(gneiss::transform_type_id(), info) != gneiss::result::success ||
      registry.find_type(gneiss::camera_type_id(), info) != gneiss::result::success) {
    return 7;
  }
  gneiss::world second{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): world 明确定义了可查询的移动后状态。
  if (first.is_valid() || !second.is_valid() ||
      second.destroy_entity(entity) != gneiss::result::success) {
    return 5;
  }
  if (first.entity_count(count) != gneiss::result::invalid_handle ||
      first.get_parent(child, parent) != gneiss::result::invalid_handle || parent.is_valid() ||
      first.get_active_camera(active_camera) != gneiss::result::invalid_handle ||
      active_camera.is_valid()) {
    return 8;
  }
  return 0;
}
