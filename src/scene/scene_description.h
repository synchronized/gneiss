// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_DESCRIPTION_H_
#define GNEISS_SCENE_SCENE_DESCRIPTION_H_

#include <gneiss/core/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::scene_internal {

struct scene_diagnostic final {
  gneiss_result result = GNEISS_SUCCESS;
  std::size_t byte_offset = 0;
  std::string path;
  std::string message;
};

struct camera_description final {
  float vertical_field_of_view_radians = 0.0F;
  float near_plane = 0.0F;
  float far_plane = 0.0F;
  bool is_primary = false;
};

struct mesh_renderer_description final {
  std::string mesh_uri;
  std::string material_uri;
};

struct object_description final {
  std::string uuid;
  std::optional<std::string> parent_uuid;
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{};
  std::array<float, 3> scale{};
  std::optional<camera_description> camera;
  std::optional<mesh_renderer_description> mesh_renderer;
};

struct scene_description final {
  std::uint32_t source_schema_version = 0;
  std::string uuid;
  std::vector<object_description> objects;
  std::string author_json;
};

[[nodiscard]] gneiss_result parse_scene_description(std::string_view json,
                                                    scene_description& out_scene,
                                                    scene_diagnostic& out_diagnostic) noexcept;

[[nodiscard]] gneiss_result
load_scene_description(const asset_internal::virtual_file_system& file_system, std::string_view uri,
                       scene_description& out_scene, scene_diagnostic& out_diagnostic) noexcept;

/** 输出迁移后的当前 Schema 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] gneiss_result serialize_scene_description(const scene_description& scene,
                                                        std::string& out_json) noexcept;

} // namespace gneiss::scene_internal

#endif
