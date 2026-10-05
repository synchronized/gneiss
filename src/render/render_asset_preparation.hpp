// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/asset/material_parameters.hpp"
#include "engine/asset/mesh_data.hpp"
#include "engine/asset/texture_preparation_data.hpp"

#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::render_internal {

using texture_prepare_profile = asset_internal::texture_prepare_profile;

struct asset_diagnostic final {
  gneiss_result result = GNEISS_SUCCESS;
  std::size_t byte_offset = 0;
  std::string path;
  std::string message;
};

/** 纯 CPU 准备：不访问缓存、RID 表或 GPU；调用方拥有只读 VFS 快照及输出。
 * 有界读取需要后端支持；verify_source 拒绝准备期间变化的描述或负载，不提供跨进程事务隔离。 */
[[nodiscard]] gneiss_result
prepare_texture(const asset_internal::virtual_file_system& file_system, std::string_view uri,
                asset_internal::prepared_texture_data& output, asset_diagnostic& diagnostic,
                std::size_t input_limit = std::numeric_limits<std::size_t>::max(),
                bool verify_source = false, std::size_t output_limit = 256U * 1024U * 1024U,
                texture_prepare_profile profile = {}) noexcept;

enum class render_asset_type : std::uint32_t {
  invalid = 0U,
  mesh = 1U,
  material = 2U,
  texture = 3U
};

struct render_asset_reload final {
  std::string uri;
  render_asset_type type = render_asset_type::mesh;
};

/** 后台解析后的自有 CPU 数据；不持有缓存或后端对象，材质依赖仅使用 URI。 */
struct prepared_render_asset {
  render_asset_reload source;
  asset_internal::mesh_data mesh;
  asset_internal::prepared_texture_data texture{};
  asset_internal::material_parameters material{};
  std::array<std::string, 5> texture_uris;
  std::size_t bytes{};
};
struct prepared_render_batch {
  std::vector<prepared_render_asset> assets;
  std::size_t bytes{};
  /** 整文件快照持有的输入字节；区间读取没有源副本，不计入此值。 */
  std::size_t input_bytes{};
};
[[nodiscard]] gneiss_result
prepare_render_assets(const asset_internal::virtual_file_system& file_system,
                      std::span<const render_asset_reload> requested, prepared_render_batch& output,
                      asset_diagnostic& diagnostic, const std::function<bool()>& cancelled,
                      std::size_t maximum_assets = 256U,
                      std::size_t maximum_bytes = 256U * 1024U * 1024U,
                      texture_prepare_profile profile = {}) noexcept;

} // namespace gneiss::render_internal
