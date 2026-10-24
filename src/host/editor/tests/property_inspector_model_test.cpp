// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/c_owner.hpp"
#include "engine/core/result.hpp"
#include "engine/values/world.hpp"
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
  gneiss::test_support::world_owner world;
  gneiss::entity_id entity;
  gneiss::entity_id transform_only;
  gneiss::scene_node_id entity_node;
  gneiss::scene_node_id transform_only_node;
  gneiss::editor::property_inspector_model inspector;
  if (([&] {
        const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
        return gneiss::engine::result{gneiss_world_create(&desc, world.put())};
      })() != gneiss::engine::result::success ||
      ([&](entity_id& out_entity) {
        gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
        const auto native_result =
            gneiss::engine::result{gneiss_world_entity_create(world.get(), &native_entity)};
        if (native_result == gneiss::engine::result::success) {
          out_entity = entity_id{native_entity};
        }
        return native_result;
      })(entity) != gneiss::engine::result::success ||
      ([&](entity_id& out_entity) {
        gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
        const auto native_result =
            gneiss::engine::result{gneiss_world_entity_create(world.get(), &native_entity)};
        if (native_result == gneiss::engine::result::success) {
          out_entity = entity_id{native_entity};
        }
        return native_result;
      })(transform_only) != gneiss::engine::result::success ||
      inspector.initialize() != gneiss::engine::result::success) {
    return 1;
  }
  gneiss::camera_desc camera{};
  if (([&](scene_node_id parent, entity_id entity, scene_node_id& out_node) {
        gneiss_scene_node_id native_node = GNEISS_NULL_SCENE_NODE_ID;
        const auto native_result = gneiss::engine::result{
            gneiss_scene_node_create(world.get(), parent.get(), entity.get(), &native_node)};
        if (native_result == gneiss::engine::result::success) {
          out_node = scene_node_id{native_node};
        }
        return native_result;
      })({}, entity, entity_node) != gneiss::engine::result::success ||
      ([&](scene_node_id parent, entity_id entity, scene_node_id& out_node) {
        gneiss_scene_node_id native_node = GNEISS_NULL_SCENE_NODE_ID;
        const auto native_result = gneiss::engine::result{
            gneiss_scene_node_create(world.get(), parent.get(), entity.get(), &native_node)};
        if (native_result == gneiss::engine::result::success) {
          out_node = scene_node_id{native_node};
        }
        return native_result;
      })({}, transform_only, transform_only_node) != gneiss::engine::result::success ||
      ([&](entity_id entity, const camera_desc& value) {
        const auto native = to_native(value);
        return gneiss::engine::result{
            gneiss_world_entity_configure_camera(world.get(), entity.get(), &native)};
      })(entity, camera) != gneiss::engine::result::success ||
      ([&](entity_id entity) {
        return gneiss::engine::result{gneiss_world_set_active_camera(world.get(), entity.get())};
      })(gneiss::entity_id{entity}) != gneiss::engine::result::success) {
    return 2;
  }
  if (inspector.refresh(world.get(), entity) != gneiss::engine::result::success) {
    return 20;
  }
  if (inspector.components().size() != 2U) {
    return 21;
  }
  if (inspector.components()[0].properties.size() != 3U ||
      inspector.components()[1].properties.size() != 4U) {
    return 22;
  }

  gneiss::engine::property_value value;
  value.payload = gneiss::property_vec3{.x = 2.0F, .y = 3.0F, .z = 4.0F};
  if (inspector.set_value(gneiss::from_native(gneiss_transform_type_id()),
                          gneiss::field_id{GNEISS_TRANSFORM_FIELD_TRANSLATION},
                          value) != gneiss::engine::result::success) {
    return 3;
  }
  gneiss::transform transform{};
  if (([&](entity_id entity, gneiss::transform& output) {
        gneiss_transform native{};
        const auto status = gneiss::engine::result{
            gneiss_world_entity_get_local_transform(world.get(), entity.get(), &native)};
        if (status.ok()) {
          output = from_native(native);
        }
        return status;
      })(entity, transform) != gneiss::engine::result::success ||
      !nearly_equal(transform.translation[0], 2.0F)) {
    return 4;
  }

  value.payload = gneiss::property_vec3{.x = 0.0F, .y = 1.0F, .z = 1.0F};
  if (inspector.set_value(gneiss::from_native(gneiss_transform_type_id()),
                          gneiss::field_id{GNEISS_TRANSFORM_FIELD_SCALE},
                          value) != gneiss::engine::result::invalid_argument ||
      ([&](entity_id entity, gneiss::transform& output) {
        gneiss_transform native{};
        const auto status = gneiss::engine::result{
            gneiss_world_entity_get_local_transform(world.get(), entity.get(), &native)};
        if (status.ok()) {
          output = from_native(native);
        }
        return status;
      })(entity, transform) != gneiss::engine::result::success ||
      !nearly_equal(transform.scale[0], 1.0F) ||
      !nearly_equal(
          std::get<gneiss::property_vec3>(inspector.components()[0].properties[2].value.payload).x,
          1.0F)) {
    return 5;
  }

  value.payload = true;
  if (inspector.set_value(gneiss::from_native(gneiss_camera_type_id()),
                          gneiss::field_id{GNEISS_CAMERA_FIELD_IS_PRIMARY},
                          value) != gneiss::engine::result::unsupported) {
    return 6;
  }
  if (inspector.refresh(world.get(), transform_only) != gneiss::engine::result::success ||
      inspector.components().size() != 1U) {
    return 7;
  }
  if (([&](entity_id entity) {
        return gneiss::engine::result{gneiss_world_entity_destroy(world.get(), entity.get())};
      })(transform_only) != gneiss::engine::result::success ||
      inspector.refresh(world.get(), transform_only) != gneiss::engine::result::invalid_handle ||
      !inspector.components().empty()) {
    return 8;
  }
  // 冻结元数据可读不代表 World 可跨线程写入；不并发访问模型本身。
  auto cross_thread = gneiss::engine::result{gneiss::engine::result::success};
  std::thread worker([&] { cross_thread = inspector.refresh(world.get(), entity); });
  worker.join();
  if (cross_thread != gneiss::engine::result::invalid_state || !inspector.components().empty() ||
      inspector.refresh(world.get(), entity).failed()) {
    return 9;
  }
  return 0;
}
