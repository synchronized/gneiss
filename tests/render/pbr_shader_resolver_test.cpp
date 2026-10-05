// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/backend/granit/pbr_shader_resolver.hpp"

#include <granit/pipeline/pbr_material.h>
#include <granit/renderer/shader_library.h>

#include <array>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  const auto size = stream.tellg();
  if (size <= 0)
    return {};
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    return {};
  return bytes;
}
} // namespace

int main() {
  using gneiss::render_internal::pbr_shader_resolver;
  const auto root = std::filesystem::path{GNEISS_TEST_GRANIT_PBR_ASSET_DIR};
  pbr_shader_resolver resolver;
  if (resolver.valid() || resolver.initialize(root) != GRANIT_SUCCESS || !resolver.valid())
    return 1;
  const auto expected = read_file(root / "pbr_standard.grshlib");
  if (expected.empty() || !std::ranges::equal(expected, resolver.shader_archive()))
    return 2;
  granit_shader_library_info info = GRANIT_SHADER_LIBRARY_INFO_INIT;
  const auto archive = resolver.shader_archive();
  if (granit_shader_library_inspect(archive.data(), archive.size(), &info) != GRANIT_SUCCESS ||
      info.shader_count < 2 || info.payload_count < 2)
    return 3;
  resolver.reset();
  if (resolver.valid() || !resolver.shader_archive().empty() ||
      resolver.initialize(root / "missing") != GRANIT_ERROR_INITIALIZATION_FAILED ||
      resolver.valid())
    return 4;
  if (resolver.initialize_embedded() != GRANIT_SUCCESS || !resolver.valid() ||
      !std::ranges::equal(expected, resolver.shader_archive()))
    return 5;
  const auto material = read_file(GNEISS_TEST_GRANIT_PBR_MATERIAL);
  if (material.empty() || !std::ranges::equal(material, pbr_shader_resolver::material_archive()))
    return 6;

  const std::array attributes{granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_POSITION,
                                                      GRANIT_VERTEX_FORMAT_FLOAT32X3, 0, 0},
                              granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_NORMAL,
                                                      GRANIT_VERTEX_FORMAT_FLOAT32X3, 12, 0},
                              granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_TANGENT,
                                                      GRANIT_VERTEX_FORMAT_FLOAT32X4, 24, 0},
                              granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_UV0,
                                                      GRANIT_VERTEX_FORMAT_FLOAT32X2, 40, 0}};
  const granit_vertex_buffer_layout valid_layout{48, GRANIT_VERTEX_STEP_MODE_VERTEX,
                                                 static_cast<std::uint32_t>(attributes.size()), 0,
                                                 attributes.data()};
  if (granit_pbr_validate_vertex_layout(&valid_layout, 1, GRANIT_PBR_TEXTURE_ALL, 0U, 0U) !=
      GRANIT_PBR_VERTEX_LAYOUT_VALID)
    return 9;
  const granit_vertex_buffer_layout missing_uv_layout{48, GRANIT_VERTEX_STEP_MODE_VERTEX, 2, 0,
                                                      attributes.data()};
  if (granit_pbr_validate_vertex_layout(&missing_uv_layout, 1, GRANIT_PBR_TEXTURE_BASE_COLOR, 0U,
                                        0U) != GRANIT_PBR_VERTEX_LAYOUT_MISSING_UV0)
    return 10;

  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto fixture =
      std::filesystem::temp_directory_path() / ("gneiss-pbr-library-" + std::to_string(unique));
  std::filesystem::create_directories(fixture);
  const auto path = fixture / "pbr_standard.grshlib";
  // 截断与内容损坏均必须清除先前成功加载的归档，不能留下可借用的旧状态。
  for (const auto truncated : {true, false}) {
    auto damaged = expected;
    if (truncated)
      damaged.resize(damaged.size() / 2);
    else
      damaged.front() ^= std::byte{0xff};
    {
      std::ofstream output(path, std::ios::binary);
      output.write(reinterpret_cast<const char*>(damaged.data()),
                   static_cast<std::streamsize>(damaged.size()));
    }
    if (resolver.initialize_embedded() != GRANIT_SUCCESS ||
        resolver.initialize(fixture) != GRANIT_ERROR_INITIALIZATION_FAILED || resolver.valid() ||
        !resolver.shader_archive().empty()) {
      std::filesystem::remove_all(fixture);
      return 7;
    }
  }
  std::filesystem::remove_all(fixture);
  return resolver.initialize_embedded() == GRANIT_SUCCESS ? 0 : 8;
}
