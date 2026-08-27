// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_HPP_
#define GNEISS_RENDER_HPP_

#include <gneiss/render.h>

namespace gneiss {

class mesh_id final {
public:
  constexpr mesh_id() noexcept = default;
  explicit constexpr mesh_id(gneiss_mesh value) noexcept : value_(value) {}
  [[nodiscard]] constexpr gneiss_mesh get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_MESH; }

private:
  gneiss_mesh value_ = GNEISS_NULL_MESH;
};

class material_id final {
public:
  constexpr material_id() noexcept = default;
  explicit constexpr material_id(gneiss_material value) noexcept : value_(value) {}
  [[nodiscard]] constexpr gneiss_material get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_MATERIAL; }

private:
  gneiss_material value_ = GNEISS_NULL_MATERIAL;
};

class texture_id final {
public:
  constexpr texture_id() noexcept = default;
  explicit constexpr texture_id(gneiss_texture value) noexcept : value_(value) {}
  [[nodiscard]] constexpr gneiss_texture get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_TEXTURE; }

private:
  gneiss_texture value_ = GNEISS_NULL_TEXTURE;
};

using mesh_vertex = gneiss_mesh_vertex;
using mesh_desc = gneiss_mesh_desc;
using material_desc = gneiss_material_desc;
using texture_desc = gneiss_texture_desc;
using camera = gneiss_camera;
using camera_desc = gneiss_camera_desc;
using mesh_renderer = gneiss_mesh_renderer;

} // namespace gneiss

#endif
