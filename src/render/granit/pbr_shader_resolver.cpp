// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/pbr_shader_resolver.h"

#include <granit/core/shader_features.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>

namespace gneiss::application_internal {
namespace {

constexpr std::size_t manifest_digest_offset = 48;
constexpr std::size_t manifest_digest_size = 32;
constexpr std::size_t minimum_manifest_size = manifest_digest_offset + manifest_digest_size;
constexpr std::array<std::string_view, 2> shader_names{"pbr_standard.vert.grshader",
                                                       "pbr_standard.frag.grshader"};

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

bool has_manifest_magic(const std::vector<std::uint8_t>& manifest) noexcept {
  constexpr std::array<std::uint8_t, 8> magic{'G', 'R', 'N', 'S', 'H', 'D', 'R', 0};
  return manifest.size() >= minimum_manifest_size &&
         std::ranges::equal(magic, std::span{manifest}.first(magic.size()));
}

} // namespace

granit_result
pbr_shader_resolver::initialize(const std::filesystem::path& asset_directory) noexcept {
  reset();
  try {
    for (std::size_t index = 0; index < assets_.size(); ++index) {
      const auto base = asset_directory / shader_names[index];
      auto& target = assets_[index];
      if (!read_file(base, target.manifest) || !has_manifest_magic(target.manifest) ||
          !read_file(base.string() + ".spv", target.spirv) ||
          !read_file(base.string() + ".wgsl", target.wgsl)) {
        reset();
        return GRANIT_ERROR_INITIALIZATION_FAILED;
      }
      std::ranges::copy_n(target.manifest.begin() + manifest_digest_offset,
                          target.content_id.size(), target.content_id.begin());
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
