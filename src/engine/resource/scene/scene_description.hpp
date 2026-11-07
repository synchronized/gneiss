// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/resource/scene/game_object_description.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace gneiss::engine {
struct scene_diagnostic final {
  engine::error error = engine::error::success;
  std::size_t byte_offset = 0;
  std::string path{};
  std::string message{};
};

struct scene_description final {
  inline static constexpr std::string_view type_name = "scene_description";
  inline static constexpr engine::guid type_guid{
      engine::guid::literal("4b91d830-28b7-4257-815f-673a9e6b7011")};
  inline static constexpr std::size_t maximum_source_bytes = 16U * 1024U * 1024U;
  gneiss::engine::guid uuid;
  std::vector<game_object_description> objects;
};

} // namespace gneiss::engine
