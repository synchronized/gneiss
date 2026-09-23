// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_PBR_SHADER_RESOLVER_H_
#define GNEISS_RENDER_GRANIT_PBR_SHADER_RESOLVER_H_

#include <granit/core/result.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace gneiss::application_internal {

/** 校验并持有标准 PBR Shader Library 字节；GPU 变体解析由 Granit 负责。 */
class pbr_shader_resolver final {
public:
  [[nodiscard]] granit_result initialize(const std::filesystem::path& asset_directory) noexcept;
  [[nodiscard]] granit_result initialize_embedded() noexcept;
  /** 仅在借用 shader_archive 的 GPU Library 销毁后调用。 */
  void reset() noexcept;
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::span<const std::byte> shader_archive() const noexcept;
  [[nodiscard]] static std::span<const std::byte> material_archive() noexcept;

private:
  [[nodiscard]] granit_result validate() noexcept;
  std::vector<std::byte> archive_;
};

} // namespace gneiss::application_internal

#endif
