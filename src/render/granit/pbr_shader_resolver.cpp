// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/pbr_shader_resolver.h"

#include <granit/renderer/shader_library.h>

#include <cstdint>
#include <fstream>
#include <limits>

namespace gneiss::application_internal {
namespace {

constexpr std::uint8_t shader_library_bytes[] = {
#include "pbr_standard.grshlib.inc"
};
constexpr std::uint8_t material_archive_bytes[] = {
#include "pbr_standard.grmat.inc"
};

} // namespace

granit_result pbr_shader_resolver::validate() noexcept {
  granit_shader_library_info info = GRANIT_SHADER_LIBRARY_INFO_INIT;
  if (granit_shader_library_inspect(archive_.data(), archive_.size(), &info) != GRANIT_SUCCESS) {
    reset();
    return GRANIT_ERROR_INITIALIZATION_FAILED;
  }
  return GRANIT_SUCCESS;
}

granit_result
pbr_shader_resolver::initialize(const std::filesystem::path& asset_directory) noexcept {
  reset();
  try {
    std::ifstream stream(asset_directory / "pbr_standard.grshlib",
                         std::ios::binary | std::ios::ate);
    if (!stream) {
      return GRANIT_ERROR_INITIALIZATION_FAILED;
    }
    const auto end = stream.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::size_t>::max()) {
      return GRANIT_ERROR_INITIALIZATION_FAILED;
    }
    archive_.resize(static_cast<std::size_t>(end));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(archive_.data()), end)) {
      reset();
      return GRANIT_ERROR_INITIALIZATION_FAILED;
    }
    return validate();
  } catch (...) {
    reset();
    return GRANIT_ERROR_OUT_OF_MEMORY;
  }
}

granit_result pbr_shader_resolver::initialize_embedded() noexcept {
  reset();
  try {
    const auto bytes = std::as_bytes(std::span{shader_library_bytes});
    archive_.assign(bytes.begin(), bytes.end());
    return validate();
  } catch (...) {
    reset();
    return GRANIT_ERROR_OUT_OF_MEMORY;
  }
}

void pbr_shader_resolver::reset() noexcept { archive_.clear(); }

bool pbr_shader_resolver::valid() const noexcept { return !archive_.empty(); }

std::span<const std::byte> pbr_shader_resolver::shader_archive() const noexcept { return archive_; }

std::span<const std::byte> pbr_shader_resolver::material_archive() noexcept {
  return std::as_bytes(std::span{material_archive_bytes});
}

} // namespace gneiss::application_internal
