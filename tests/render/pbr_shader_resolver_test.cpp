// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/pbr_shader_resolver.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

std::array<std::uint8_t, 32> read_content_id(const std::filesystem::path& manifest) {
  std::array<std::uint8_t, 32> result{};
  std::ifstream stream(manifest, std::ios::binary);
  stream.seekg(48);
  stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
  return result;
}

} // namespace

int main() {
  using gneiss::application_internal::pbr_shader_resolver;
  const auto root = std::filesystem::path{GNEISS_TEST_GRANIT_PBR_ASSET_DIR};
  pbr_shader_resolver resolver;
  if (resolver.initialize(root) != GRANIT_SUCCESS || !resolver.valid())
    return 1;

  const auto vertex_id = read_content_id(root / "pbr_standard.vert.grshader");
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
  return 0;
}
