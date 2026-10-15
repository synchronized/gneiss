// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_

#include "engine/asset/resource_cache.hpp"
#include "engine/function/render/render_asset_preparation.hpp"
#include "engine/function/render/render_resource_data.hpp"

#include <gneiss/engine/render.h>

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

class render_asset_lease {
public:
  [[nodiscard]] std::uint64_t get() const noexcept;
  [[nodiscard]] render_asset_type type() const noexcept {
    return entry_ ? static_cast<render_asset_type>(entry_->type) : render_asset_type::invalid;
  }
  [[nodiscard]] std::string_view uri() const noexcept {
    return entry_ ? entry_->uri : std::string_view{};
  }
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const asset_internal::resource_cache::entry> entry_;
};

/** 上传候选只携带不可变 CPU 数据；后端所有权留在渲染服务。 */
struct render_upload_item {
  std::shared_ptr<const mesh_resource> mesh;
  std::shared_ptr<const material_resource> material;
  std::shared_ptr<const texture_resource> texture;
  std::array<std::shared_ptr<const texture_resource>, 5> dependency_textures;
  std::size_t bytes{};
  std::shared_ptr<const std::vector<std::byte>> texture_payload{};
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

/** resources 必须晚于缓存及其所有资产租约销毁；租约析构会释放对应 RID。 */
class render_asset_loader final {
public:
  enum class publication_mode { reload, fixed_version };
  struct asset_candidate {
    publication_mode mode{publication_mode::reload};
    asset_internal::asset_version_key version;
    render_asset_reload source;
    render_asset_lease lease;
    bool existed{};
    std::weak_ptr<const asset_internal::resource_cache::entry> expected;
    std::shared_ptr<const mesh_resource> mesh;
    std::shared_ptr<const material_resource> material;
    std::shared_ptr<const texture_resource> texture;
    std::shared_ptr<const void> previous;
    std::array<std::shared_ptr<const asset_internal::resource_cache::entry>, 5> dependencies;
    std::array<std::shared_ptr<const texture_resource>, 5> dependency_textures;
    std::size_t bytes{};
    std::shared_ptr<const std::vector<std::byte>> texture_payload{};
  };
  using revision_stamp = std::pair<std::uint64_t, std::uint64_t>;
  /** 完整编码当前准备配置；保守隔离不同设备代次及格式能力，不读取来源。 */
  [[nodiscard]] static asset_internal::resource_cache::selection_key
  selection_key(const asset_internal::asset_identity& identity, texture_prepare_profile profile);
  /** 仅取得已采用且就绪的租约；缺失返回 NOT_READY，不加载或等待。 */
  [[nodiscard]] gneiss_result acquire_selected(const asset_internal::asset_identity& identity,
                                               texture_prepare_profile profile,
                                               render_asset_lease& output) const noexcept;
  [[nodiscard]] gneiss_result acquire_selected(const render_asset_reload& source,
                                               texture_prepare_profile profile,
                                               render_asset_lease& output) const noexcept;
  /** profile 非零时校验纹理及材质纹理依赖的设备身份；零值供逻辑层读取已发布 RID。 */
  [[nodiscard]] gneiss_result acquire_cached(const render_asset_reload& source,
                                             render_asset_lease& output,
                                             texture_prepare_profile profile = {}) const noexcept;
  [[nodiscard]] revision_stamp revision() const noexcept { return {cache_.revision(), revision_}; }
  /** 固定版本模式要求已复验的版本键；existed 表示已发布复用，调用方应跳过上传。
   * 新候选拥有独立 RID，未发布前销毁即撤回；GPU 上传须先确认或撤回再释放候选。
   * 不可混合两种发布模式；材质依赖须先以确切版本准备并置于 staged。 */
  [[nodiscard]] gneiss_result
  stage_asset(prepared_render_asset prepared, std::span<const asset_candidate> staged,
              asset_candidate& output, publication_mode mode = publication_mode::reload) noexcept;
  [[nodiscard]] gneiss_result publish_assets(std::span<asset_candidate> candidates) noexcept;
  [[nodiscard]] texture_asset_lease texture_lease(const render_asset_lease& lease) const noexcept;
  [[nodiscard]] static mesh_asset_lease mesh_lease(const render_asset_lease& lease) noexcept;
  [[nodiscard]] static material_asset_lease
  material_lease(const render_asset_lease& lease) noexcept;
  [[nodiscard]] bool owns(const render_asset_lease& lease) const noexcept;
  /** 保留场景实际持有的版本，不查询 URI 缓存；空租约仍为空。 */
  [[nodiscard]] static render_asset_lease asset_lease(const mesh_asset_lease& lease) noexcept;
  [[nodiscard]] static render_asset_lease asset_lease(const material_asset_lease& lease) noexcept;
  /** 返回指定材质版本持有的纹理租约；非材质返回空数组，不读取来源或缓存。 */
  [[nodiscard]] static std::array<render_asset_lease, 5>
  texture_dependencies(const render_asset_lease& material) noexcept;
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
  [[nodiscard]] gneiss_result stage_texture(const texture_target& target,
                                            asset_internal::prepared_texture_data prepared,
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
  std::uint64_t revision_{};
  const asset_internal::virtual_file_system& file_system_;
  asset_internal::resource_cache& cache_;
  render_resource_service& resources_;
};

} // namespace gneiss::render_internal

#endif
