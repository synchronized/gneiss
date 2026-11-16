// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ui/viewport/editor_camera.hpp"
#include <gneiss/engine/error.hpp>

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

gneiss::error editor_camera::initialize() {
  if (initialized_) {
    return gneiss::error::invalid_state;
  }
  initialized_ = true;
  transform_ = {};
  transform_.translation[1] = 2.0F;
  transform_.translation[2] = 6.0F;
  update_rotation();
  return gneiss::error::success;
}

void editor_camera::shutdown() noexcept {
  initialized_ = false;
  transform_ = {};
  yaw_ = 0.0F;
  pitch_ = -0.3F;
}

gneiss::math::camera_settings editor_camera::settings() const noexcept {
  return {.fov = 1.04719755F, .near_plane = 0.05F, .far_plane = 2000.0F};
}

void editor_camera::update_rotation() noexcept {
  const auto yaw_half = yaw_ * 0.5F;
  const auto pitch_half = pitch_ * 0.5F;
  transform_.rotation[0] = std::cos(yaw_half) * std::sin(pitch_half);
  transform_.rotation[1] = std::sin(yaw_half) * std::cos(pitch_half);
  transform_.rotation[2] = -std::sin(yaw_half) * std::sin(pitch_half);
  transform_.rotation[3] = std::cos(yaw_half) * std::cos(pitch_half);
}

gneiss::error editor_camera::update(const editor_camera_input& input) {
  if (!is_valid()) {
    return gneiss::error::invalid_state;
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
  return gneiss::error::success;
}

gneiss::error editor_camera::focus(const gneiss::math::transform& target,
                                           float distance) noexcept {
  if (!is_valid() || !std::isfinite(distance) || distance <= 0.0F) {
    return gneiss::error::invalid_argument;
  }
  const auto cosine_pitch = std::cos(pitch_);
  const float forward[3] = {-std::sin(yaw_) * cosine_pitch, std::sin(pitch_),
                            -std::cos(yaw_) * cosine_pitch};
  for (std::size_t index = 0; index < 3U; ++index) {
    transform_.translation[index] = target.translation[index] - (forward[index] * distance);
  }
  return gneiss::error::success;
}

} // namespace gneiss::editor
