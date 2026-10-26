// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "panels/property_inspector_model.hpp"

#include <cmath>
#include <thread>

using namespace gneiss;

namespace {

[[nodiscard]] bool nearly_equal(float left, float right) noexcept {
  return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
  gneiss::engine::world_owner owner;

  gneiss::entity_id entity;
  gneiss::entity_id transform_only;
  gneiss::scene_node_id entity_node;
  gneiss::scene_node_id transform_only_node;
  gneiss::editor::property_inspector_model inspector;
  if (gneiss::engine::create_native(owner) != gneiss::engine::result::success ||
      owner->entity_create(entity) != gneiss::engine::result::success ||
      owner->entity_create(transform_only) != gneiss::engine::result::success ||
      inspector.initialize() != gneiss::engine::result::success) {
    return 1;
  }
  gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (owner->node_create({}, entity, entity_node) != gneiss::engine::result::success ||
      owner->node_create({}, transform_only, transform_only_node) !=
          gneiss::engine::result::success ||
      owner->entity_configure_camera(entity, camera) != gneiss::engine::result::success ||
      owner->set_active_camera(gneiss::entity_id{entity}) != gneiss::engine::result::success) {
    return 2;
  }
  if (inspector.refresh(owner.get(), entity) != gneiss::engine::result::success) {
    return 20;
  }
  if (inspector.components().size() != 2U) {
    return 21;
  }
  if (inspector.components()[0].properties.size() != 3U ||
      inspector.components()[1].properties.size() != 4U) {
    return 22;
  }

  gneiss::engine::value value;
  value.payload = gneiss::property_vec3{2.0F, 3.0F, 4.0F};
  if (inspector.set_value(gneiss::engine::transform_component_type,
                          gneiss::engine::transform_translation_field,
                          value) != gneiss::engine::result::success) {
    return 3;
  }
  gneiss::engine::transform transform{};
  if (owner->entity_get_local_transform(entity, transform) != gneiss::engine::result::success ||
      !nearly_equal(transform.translation[0], 2.0F)) {
    return 4;
  }

  value.payload = gneiss::property_vec3{0.0F, 1.0F, 1.0F};
  if (inspector.set_value(gneiss::engine::transform_component_type,
                          gneiss::engine::transform_scale_field,
                          value) != gneiss::engine::result::invalid_argument ||
      owner->entity_get_local_transform(entity, transform) != gneiss::engine::result::success ||
      !nearly_equal(transform.scale[0], 1.0F) ||
      !nearly_equal(
          std::get<gneiss::property_vec3>(inspector.components()[0].properties[2].value.payload).x,
          1.0F)) {
    return 5;
  }

  value.payload = true;
  if (inspector.set_value(gneiss::engine::camera_component_type,
                          gneiss::engine::camera_is_primary_field,
                          value) != gneiss::engine::result::unsupported) {
    return 6;
  }
  if (inspector.refresh(owner.get(), transform_only) != gneiss::engine::result::success ||
      inspector.components().size() != 1U) {
    return 7;
  }
  if (owner->entity_destroy(transform_only) != gneiss::engine::result::success ||
      inspector.refresh(owner.get(), transform_only) != gneiss::engine::result::invalid_handle ||
      !inspector.components().empty()) {
    return 8;
  }
  // 冻结元数据可读不代表 World 可跨线程写入；不并发访问模型本身。
  auto cross_thread = gneiss::engine::result{gneiss::engine::result::success};
  std::thread worker([&] { cross_thread = inspector.refresh(owner.get(), entity); });
  worker.join();
  if (cross_thread != gneiss::engine::result::invalid_state || !inspector.components().empty() ||
      inspector.refresh(owner.get(), entity).failed()) {
    return 9;
  }
  return 0;
}
