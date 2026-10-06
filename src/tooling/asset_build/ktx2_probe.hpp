// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace gneiss::tooling::asset_build {

enum class ktx2_probe_result : std::uint8_t {
  success,
  not_found,
  invalid_container,
  unsupported_container,
  io_error,
};

enum class ktx2_transfer_function : std::uint8_t { linear, srgb, unspecified };

enum class ktx2_supercompression : std::uint8_t { none, basis_lz, zstd, zlib, unknown };

struct ktx2_information final {
  std::uint32_t vk_format{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t depth{};
  std::uint32_t layer_count{};
  std::uint32_t face_count{};
  std::uint32_t level_count{};
  ktx2_transfer_function transfer_function{ktx2_transfer_function::unspecified};
  ktx2_supercompression supercompression{ktx2_supercompression::unknown};
  bool has_alpha{};
};

struct ktx2_probe_report final {
  ktx2_probe_result result{ktx2_probe_result::invalid_container};
  ktx2_information information;
  std::string diagnostic;
};

/** 只检查 KTX2 容器、级别范围及上传相关元数据，不解码或转码图像。 */
[[nodiscard]] ktx2_probe_report inspect_ktx2(const std::filesystem::path& path);

} // namespace gneiss::tooling::asset_build
