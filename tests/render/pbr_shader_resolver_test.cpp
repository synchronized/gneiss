// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/pbr_shader_resolver.h"

#include <granit/pipeline/pbr_material.h>
#include <granit/renderer/shader.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  const auto size = stream.tellg();
  std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size));
  return result;
}

std::array<std::uint8_t, 32> inspect_content_id(const std::filesystem::path& manifest) {
  const auto bytes = read_file(manifest);
  granit_shader_asset_info info = GRANIT_SHADER_ASSET_INFO_INIT;
  std::array<char, 64> entry_point{};
  info.entry_point = entry_point.data();
  info.entry_point_capacity = static_cast<std::uint32_t>(entry_point.size());
  if (granit_shader_asset_inspect(bytes.data(), bytes.size(), &info) != GRANIT_SUCCESS)
    return {};
  std::array<std::uint8_t, 32> result{};
  std::ranges::copy(info.content_id, result.begin());
  return result;
}

void copy_shader_assets(const std::filesystem::path& source,
                        const std::filesystem::path& destination) {
  constexpr std::array<std::string_view, 6> names{
      "pbr_standard.vert.grshader",      "pbr_standard.vert.grshader.spv",
      "pbr_standard.vert.grshader.wgsl", "pbr_standard.frag.grshader",
      "pbr_standard.frag.grshader.spv",  "pbr_standard.frag.grshader.wgsl"};
  std::filesystem::create_directories(destination);
  for (const auto name : names) {
    std::filesystem::copy_file(source / name, destination / name,
                               std::filesystem::copy_options::overwrite_existing);
  }
}

} // namespace

int main() {
  using gneiss::application_internal::pbr_shader_resolver;
  const auto root = std::filesystem::path{GNEISS_TEST_GRANIT_PBR_ASSET_DIR};
  pbr_shader_resolver resolver;
  if (resolver.initialize(root) != GRANIT_SUCCESS || !resolver.valid())
    return 1;

  const auto vertex_id = inspect_content_id(root / "pbr_standard.vert.grshader");
  granit_shader_asset_desc asset = GRANIT_SHADER_ASSET_DESC_INIT;
  if (pbr_shader_resolver::resolve(&resolver, vertex_id.data(), GRANIT_RENDERER_BACKEND_VULKAN,
                                   GRANIT_SHADER_PROFILE_PORTABLE, &asset) != GRANIT_SUCCESS ||
      asset.manifest_size == 0 || asset.sidecar_size == 0)
    return 2;
  if (pbr_shader_resolver::resolve(&resolver, vertex_id.data(), GRANIT_RENDERER_BACKEND_WEBGPU,
                                   GRANIT_SHADER_PROFILE_PORTABLE, &asset) != GRANIT_SUCCESS ||
      asset.sidecar_size == 0)
    return 3;
  if (pbr_shader_resolver::resolve(&resolver, vertex_id.data(), GRANIT_RENDERER_BACKEND_VULKAN, 0,
                                   &asset) != GRANIT_ERROR_UNSUPPORTED)
    return 4;

  auto unknown_id = vertex_id;
  unknown_id.front() ^= UINT8_C(0xff);
  if (pbr_shader_resolver::resolve(&resolver, unknown_id.data(), GRANIT_RENDERER_BACKEND_VULKAN,
                                   GRANIT_SHADER_PROFILE_PORTABLE,
                                   &asset) != GRANIT_ERROR_NOT_READY)
    return 5;

  resolver.reset();
  if (resolver.valid() ||
      resolver.initialize(root / "missing") != GRANIT_ERROR_INITIALIZATION_FAILED)
    return 6;
  if (resolver.initialize_embedded() != GRANIT_SUCCESS || !resolver.valid() ||
      pbr_shader_resolver::material_archive().empty())
    return 7;

  const auto standard_material = read_file(GNEISS_TEST_GRANIT_PBR_MATERIAL);
  const auto embedded_material = pbr_shader_resolver::material_archive();
  if (!std::ranges::equal(std::as_bytes(std::span{standard_material}), embedded_material))
    return 8;

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
  if (granit_pbr_validate_vertex_layout(&valid_layout, 1, GRANIT_PBR_TEXTURE_ALL) !=
      GRANIT_PBR_VERTEX_LAYOUT_VALID)
    return 9;
  const granit_vertex_buffer_layout missing_uv_layout{48, GRANIT_VERTEX_STEP_MODE_VERTEX, 2, 0,
                                                      attributes.data()};
  if (granit_pbr_validate_vertex_layout(&missing_uv_layout, 1, GRANIT_PBR_TEXTURE_BASE_COLOR) !=
      GRANIT_PBR_VERTEX_LAYOUT_MISSING_UV0)
    return 10;

  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto fixture =
      std::filesystem::temp_directory_path() / ("gneiss-pbr-resolver-" + std::to_string(unique));
  copy_shader_assets(root, fixture);
  std::filesystem::remove(fixture / "pbr_standard.vert.grshader.spv");
  if (resolver.initialize(fixture) != GRANIT_ERROR_INITIALIZATION_FAILED || resolver.valid())
    return 11;

  copy_shader_assets(root, fixture);
  {
    std::fstream manifest(fixture / "pbr_standard.vert.grshader",
                          std::ios::binary | std::ios::in | std::ios::out);
    manifest.put('X');
  }
  if (resolver.initialize(fixture) != GRANIT_ERROR_INITIALIZATION_FAILED || resolver.valid())
    return 12;
  std::filesystem::remove_all(fixture);
  return 0;
}
