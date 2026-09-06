// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_PBR_SHADER_RESOLVER_H_
#define GNEISS_RENDER_GRANIT_PBR_SHADER_RESOLVER_H_

#include <granit/pipeline/material.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace gneiss::application_internal {

/** 持有 Granit 标准 PBR Shader 资产，并按稳定内容 ID 为材质解析后端载荷。 */
class pbr_shader_resolver final {
public:
  [[nodiscard]] granit_result initialize(const std::filesystem::path& asset_directory) noexcept;
  [[nodiscard]] granit_result initialize_embedded() noexcept;
  void reset() noexcept;
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] static std::span<const std::byte> material_archive() noexcept;

  [[nodiscard]] static granit_result resolve(void* user_data, const std::uint8_t asset_id[32],
                                             granit_renderer_backend backend, std::uint32_t profile,
                                             granit_shader_asset_desc* asset) noexcept;

private:
  struct shader_asset final {
    std::array<std::uint8_t, 32> content_id{};
    std::vector<std::uint8_t> manifest;
    std::vector<std::uint8_t> spirv;
    std::vector<std::uint8_t> wgsl;
  };

  [[nodiscard]] granit_result resolve_asset(const std::uint8_t asset_id[32],
                                            granit_renderer_backend backend, std::uint32_t profile,
                                            granit_shader_asset_desc& asset) const noexcept;

  std::array<shader_asset, 2> assets_;
};

} // namespace gneiss::application_internal

#endif
