// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/pbr_shader_resolver.h"

#include <granit/core/shader_features.h>
#include <granit/renderer/shader.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>

namespace gneiss::application_internal {
namespace {

constexpr std::array<std::string_view, 2> shader_names{"pbr_standard.vert.grshader",
                                                       "pbr_standard.frag.grshader"};

alignas(std::uint32_t) constexpr std::uint8_t vertex_manifest[]{
#include "pbr_standard.vert.grshader.inc"
};
alignas(std::uint32_t) constexpr std::uint8_t vertex_spirv[]{
#include "pbr_standard.vert.grshader.spv.inc"
};
alignas(std::uint32_t) constexpr std::uint8_t vertex_wgsl[]{
#include "pbr_standard.vert.grshader.wgsl.inc"
};
alignas(std::uint32_t) constexpr std::uint8_t fragment_manifest[]{
#include "pbr_standard.frag.grshader.inc"
};
alignas(std::uint32_t) constexpr std::uint8_t fragment_spirv[]{
#include "pbr_standard.frag.grshader.spv.inc"
};
alignas(std::uint32_t) constexpr std::uint8_t fragment_wgsl[]{
#include "pbr_standard.frag.grshader.wgsl.inc"
};
alignas(std::uint32_t) constexpr std::uint8_t material_archive_bytes[]{
#include "pbr_standard.grmat.inc"
};

bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& output) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream)
    return false;
  const auto end = stream.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::size_t>::max())
    return false;
  output.resize(static_cast<std::size_t>(end));
  stream.seekg(0);
  return output.empty() ||
         static_cast<bool>(stream.read(reinterpret_cast<char*>(output.data()), end));
}

granit_result inspect_content_id(std::span<const std::uint8_t> manifest,
                                 std::array<std::uint8_t, 32>& content_id) noexcept {
  granit_shader_asset_info info = GRANIT_SHADER_ASSET_INFO_INIT;
  std::array<char, 64> entry_point{};
  info.entry_point = entry_point.data();
  info.entry_point_capacity = static_cast<std::uint32_t>(entry_point.size());
  const auto result = granit_shader_asset_inspect(manifest.data(), manifest.size(), &info);
  if (result == GRANIT_SUCCESS)
    std::ranges::copy(info.content_id, content_id.begin());
  return result;
}

} // namespace

granit_result
pbr_shader_resolver::initialize(const std::filesystem::path& asset_directory) noexcept {
  reset();
  try {
    for (std::size_t index = 0; index < assets_.size(); ++index) {
      const auto base = asset_directory / shader_names[index];
      auto& target = assets_[index];
      if (!read_file(base, target.manifest) ||
          inspect_content_id(target.manifest, target.content_id) != GRANIT_SUCCESS ||
          !read_file(base.string() + ".spv", target.spirv) ||
          !read_file(base.string() + ".wgsl", target.wgsl)) {
        reset();
        return GRANIT_ERROR_INITIALIZATION_FAILED;
      }
    }
    return GRANIT_SUCCESS;
  } catch (...) {
    reset();
    return GRANIT_ERROR_OUT_OF_MEMORY;
  }
}

granit_result pbr_shader_resolver::initialize_embedded() noexcept {
  reset();
  try {
    const std::array manifests{std::span<const std::uint8_t>{vertex_manifest},
                               std::span<const std::uint8_t>{fragment_manifest}};
    const std::array spirv{std::span<const std::uint8_t>{vertex_spirv},
                           std::span<const std::uint8_t>{fragment_spirv}};
    const std::array wgsl{std::span<const std::uint8_t>{vertex_wgsl},
                          std::span<const std::uint8_t>{fragment_wgsl}};
    for (std::size_t index = 0; index < assets_.size(); ++index) {
      auto& target = assets_[index];
      target.manifest.assign(manifests[index].begin(), manifests[index].end());
      target.spirv.assign(spirv[index].begin(), spirv[index].end());
      target.wgsl.assign(wgsl[index].begin(), wgsl[index].end());
      const auto result = inspect_content_id(target.manifest, target.content_id);
      if (result != GRANIT_SUCCESS) {
        reset();
        return result;
      }
    }
    return GRANIT_SUCCESS;
  } catch (...) {
    reset();
    return GRANIT_ERROR_OUT_OF_MEMORY;
  }
}

void pbr_shader_resolver::reset() noexcept { assets_ = {}; }

bool pbr_shader_resolver::valid() const noexcept {
  return std::ranges::all_of(assets_, [](const shader_asset& asset) {
    return !asset.manifest.empty() && !asset.spirv.empty() && !asset.wgsl.empty();
  });
}

std::span<const std::byte> pbr_shader_resolver::material_archive() noexcept {
  return std::as_bytes(std::span{material_archive_bytes});
}

granit_result pbr_shader_resolver::resolve(void* user_data, const std::uint8_t asset_id[32],
                                           granit_renderer_backend backend, std::uint32_t profile,
                                           granit_shader_asset_desc* asset) noexcept {
  if (user_data == nullptr || asset_id == nullptr || asset == nullptr)
    return GRANIT_ERROR_INVALID_ARGUMENT;
  return static_cast<const pbr_shader_resolver*>(user_data)->resolve_asset(asset_id, backend,
                                                                           profile, *asset);
}

granit_result pbr_shader_resolver::resolve_asset(const std::uint8_t asset_id[32],
                                                 granit_renderer_backend backend,
                                                 std::uint32_t profile,
                                                 granit_shader_asset_desc& asset) const noexcept {
  if (profile != GRANIT_SHADER_PROFILE_PORTABLE)
    return GRANIT_ERROR_UNSUPPORTED;
  for (const auto& candidate : assets_) {
    if (std::memcmp(candidate.content_id.data(), asset_id, candidate.content_id.size()) != 0)
      continue;
    const auto* sidecar = backend == GRANIT_RENDERER_BACKEND_VULKAN   ? &candidate.spirv
                          : backend == GRANIT_RENDERER_BACKEND_WEBGPU ? &candidate.wgsl
                                                                      : nullptr;
    if (sidecar == nullptr)
      return GRANIT_ERROR_UNSUPPORTED;
    asset = GRANIT_SHADER_ASSET_DESC_INIT;
    asset.manifest_data = candidate.manifest.data();
    asset.manifest_size = candidate.manifest.size();
    asset.sidecar_data = sidecar->data();
    asset.sidecar_size = sidecar->size();
    return GRANIT_SUCCESS;
  }
  return GRANIT_ERROR_NOT_READY;
}

} // namespace gneiss::application_internal
