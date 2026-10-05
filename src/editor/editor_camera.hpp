// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_CAMERA_H_
#define GNEISS_APPS_EDITOR_EDITOR_CAMERA_H_

#include <gneiss/scene.hpp>

namespace gneiss::editor {

struct editor_camera_input final {
  float delta_seconds = 0.0F;
  float move_forward = 0.0F;
  float move_right = 0.0F;
  float move_up = 0.0F;
  float yaw_delta = 0.0F;
  float pitch_delta = 0.0F;
  float dolly = 0.0F;
};

class editor_camera final {
public:
  editor_camera() noexcept = default;
  ~editor_camera() noexcept { shutdown(); }

  editor_camera(const editor_camera&) = delete;
  editor_camera& operator=(const editor_camera&) = delete;

  [[nodiscard]] result initialize(gneiss_world world) noexcept;
  void shutdown() noexcept;

  [[nodiscard]] bool is_valid() const noexcept { return entity_.is_valid(); }
  [[nodiscard]] result update(const editor_camera_input& input) noexcept;
  [[nodiscard]] result focus(const transform& target, float distance = 5.0F) noexcept;
  [[nodiscard]] const transform& current_transform() const noexcept { return transform_; }

private:
  [[nodiscard]] result commit_transform() noexcept;
  void update_rotation() noexcept;

  gneiss_world world_ = GNEISS_NULL_WORLD;
  entity_id entity_;
  scene_node_id node_;
  transform transform_ = GNEISS_TRANSFORM_IDENTITY;
  float yaw_ = 0.0F;
  float pitch_ = -0.3F;
};

} // namespace gneiss::editor

#endif
