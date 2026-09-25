// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_H_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_H_

#include "asset/resource_cache.h"
#include "render/render_resource_service.h"
#include <limits>

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

/** 纯 CPU 准备：不访问缓存、RID 表或 GPU；调用方拥有只读 VFS 快照及输出。
 * 有界读取需要后端支持；verify_source 拒绝准备期间变化的描述或负载，不提供跨进程事务隔离。 */
[[nodiscard]] gneiss_result
prepare_texture(const asset_internal::virtual_file_system& file_system, std::string_view uri,
                texture_resource& output, asset_diagnostic& diagnostic,
                std::size_t input_limit = std::numeric_limits<std::size_t>::max(),
                bool verify_source = false,
                std::size_t output_limit = 256U * 1024U * 1024U) noexcept;

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
  struct texture_target {
    std::string uri;
    std::weak_ptr<const asset_internal::resource_cache::entry> expected;
    bool existed{};
  };
  struct texture_candidate {
    texture_target target;
    texture_asset_lease lease;
    std::shared_ptr<const texture_resource> data;
    std::shared_ptr<const texture_resource> previous;
  };
  [[nodiscard]] gneiss_result observe_texture(std::string_view uri,
                                              texture_target& output) const noexcept;
  [[nodiscard]] gneiss_result stage_texture(const texture_target& target, texture_resource prepared,
                                            texture_candidate& output) noexcept;
  /** 全部身份仍有效才一次发布；调用方须先完成 GPU 候选确认，且不并发访问缓存。 */
  [[nodiscard]] gneiss_result publish_textures(std::span<texture_candidate> candidates) noexcept;
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
