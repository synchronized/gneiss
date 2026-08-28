// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_camera.h"

#include <gneiss/render.h>
#include <gneiss/world.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gneiss::editor {
namespace {

constexpr float movement_speed = 5.0F;
constexpr float dolly_step = 1.5F;
constexpr float pitch_limit = 1.5F;
constexpr float maximum_delta_seconds = 0.1F;

} // namespace

result editor_camera::initialize(gneiss_world world) noexcept {
  if (world == GNEISS_NULL_WORLD || is_valid()) {
    return result::invalid_argument;
  }
  world_ = world;
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  auto operation = gneiss_world_entity_create(world_, &entity);
  if (operation != GNEISS_SUCCESS) {
    world_ = GNEISS_NULL_WORLD;
    return from_native(operation);
  }
  entity_ = entity_id{entity};
  gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
  operation = gneiss_scene_node_create(world_, GNEISS_NULL_SCENE_NODE_ID, entity, &node);
  if (operation == GNEISS_SUCCESS) {
    node_ = scene_node_id{node};
    gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
    camera.near_plane = 0.05F;
    camera.far_plane = 2000.0F;
    operation = gneiss_world_entity_configure_camera(world_, entity, &camera);
  }
  if (operation == GNEISS_SUCCESS) {
    transform_.translation[1] = 2.0F;
    transform_.translation[2] = 6.0F;
    update_rotation();
    operation = to_native(commit_transform());
  }
  if (operation == GNEISS_SUCCESS) {
    operation = gneiss_world_set_active_camera(world_, entity);
  }
  if (operation != GNEISS_SUCCESS) {
    shutdown();
  }
  return from_native(operation);
}

void editor_camera::shutdown() noexcept {
  if (world_ != GNEISS_NULL_WORLD) {
    if (entity_.is_valid()) {
      (void)gneiss_world_entity_destroy(world_, entity_.get());
    }
    if (node_.is_valid()) {
      (void)gneiss_scene_node_destroy(world_, node_.get());
    }
  }
  world_ = GNEISS_NULL_WORLD;
  entity_ = {};
  node_ = {};
  transform_ = GNEISS_TRANSFORM_IDENTITY;
  yaw_ = 0.0F;
  pitch_ = -0.3F;
}

void editor_camera::update_rotation() noexcept {
  const auto yaw_half = yaw_ * 0.5F;
  const auto pitch_half = pitch_ * 0.5F;
  transform_.rotation[0] = std::cos(yaw_half) * std::sin(pitch_half);
  transform_.rotation[1] = std::sin(yaw_half) * std::cos(pitch_half);
  transform_.rotation[2] = -std::sin(yaw_half) * std::sin(pitch_half);
  transform_.rotation[3] = std::cos(yaw_half) * std::cos(pitch_half);
}

result editor_camera::commit_transform() noexcept {
  if (!is_valid()) {
    return result::invalid_state;
  }
  return from_native(gneiss_scene_node_set_local_transform(world_, node_.get(), &transform_));
}

result editor_camera::update(const editor_camera_input& input) noexcept {
  if (!is_valid()) {
    return result::invalid_state;
  }
  yaw_ += input.yaw_delta;
  pitch_ = std::clamp(pitch_ + input.pitch_delta, -pitch_limit, pitch_limit);
  update_rotation();

  const auto cosine_pitch = std::cos(pitch_);
  const float forward[3] = {-std::sin(yaw_) * cosine_pitch, std::sin(pitch_),
                            -std::cos(yaw_) * cosine_pitch};
  const float right[3] = {std::cos(yaw_), 0.0F, -std::sin(yaw_)};
  const auto delta_seconds = std::clamp(input.delta_seconds, 0.0F, maximum_delta_seconds);
  const auto forward_distance =
      (input.move_forward * movement_speed * delta_seconds) + (input.dolly * dolly_step);
  const auto right_distance = input.move_right * movement_speed * delta_seconds;
  const auto up_distance = input.move_up * movement_speed * delta_seconds;
  for (std::size_t index = 0; index < 3U; ++index) {
    transform_.translation[index] +=
        (forward[index] * forward_distance) + (right[index] * right_distance);
  }
  transform_.translation[1] += up_distance;
  return commit_transform();
}

result editor_camera::focus(const transform& target, float distance) noexcept {
  if (!is_valid() || !std::isfinite(distance) || distance <= 0.0F) {
    return result::invalid_argument;
  }
  const auto cosine_pitch = std::cos(pitch_);
  const float forward[3] = {-std::sin(yaw_) * cosine_pitch, std::sin(pitch_),
                            -std::cos(yaw_) * cosine_pitch};
  for (std::size_t index = 0; index < 3U; ++index) {
    transform_.translation[index] = target.translation[index] - (forward[index] * distance);
  }
  return commit_transform();
}

} // namespace gneiss::editor
