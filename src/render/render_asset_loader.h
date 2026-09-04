// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_H_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_H_

#include "asset/resource_cache.h"

#include <gneiss/render.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::render_internal {

class render_resource_service;

struct asset_diagnostic final {
  gneiss_result result = GNEISS_SUCCESS;
  std::size_t byte_offset = 0;
  std::string path;
  std::string message;
};

enum class render_asset_type : std::uint32_t { mesh = 1U, material = 2U, texture = 3U };

struct render_asset_reload final {
  std::string uri;
  render_asset_type type = render_asset_type::mesh;
};

class mesh_asset_lease final {
public:
  [[nodiscard]] gneiss_mesh get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const asset_internal::resource_cache::entry> entry_;
};

class material_asset_lease final {
public:
  [[nodiscard]] gneiss_material get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const asset_internal::resource_cache::entry> entry_;
};

class texture_asset_lease final {
public:
  [[nodiscard]] gneiss_texture get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const asset_internal::resource_cache::entry> entry_;
};

class render_asset_loader final {
public:
  render_asset_loader(const asset_internal::virtual_file_system& file_system,
                      asset_internal::resource_cache& cache,
                      render_resource_service& resources) noexcept;

  [[nodiscard]] gneiss_result acquire_mesh(std::string_view uri, mesh_asset_lease& out_lease,
                                           asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss_result acquire_material(std::string_view uri,
                                               material_asset_lease& out_lease,
                                               asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss_result acquire_texture(std::string_view uri, texture_asset_lease& out_lease,
                                              asset_diagnostic& out_diagnostic) noexcept;
  /** 按依赖顺序构造并原子提交一组渲染资源。 */
  [[nodiscard]] gneiss_result reload_assets(std::span<const render_asset_reload> assets,
                                            asset_diagnostic& out_diagnostic) noexcept;
  void release_unused() noexcept { cache_.release_unused(); }

private:
  const asset_internal::virtual_file_system& file_system_;
  asset_internal::resource_cache& cache_;
  render_resource_service& resources_;
};

} // namespace gneiss::render_internal

#endif
