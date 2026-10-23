// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_

#include "engine/core/rid.hpp"
#include "engine/function/render/render_asset_preparation.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/resource/cache/resource_cache.hpp"

#include <gneiss/engine/core/result.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::engine::resource {
class virtual_file_system;
}

namespace gneiss::engine::render {

class render_resource_service;

class render_asset_lease {
public:
  /** 比较是否持有同一缓存条目；普通资产内容可更新，不读取或刷新来源。 */
  [[nodiscard]] bool operator==(const render_asset_lease&) const noexcept = default;
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
  std::shared_ptr<const engine::resource::resource_cache::entry> entry_;
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
  [[nodiscard]] engine::core::rid get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const engine::resource::resource_cache::entry> entry_;
};

class material_asset_lease final {
public:
  [[nodiscard]] engine::core::rid get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const engine::resource::resource_cache::entry> entry_;
};

class texture_asset_lease final {
public:
  [[nodiscard]] engine::core::rid get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class render_asset_loader;
  std::shared_ptr<const engine::resource::resource_cache::entry> entry_;
};

/** resources 必须晚于缓存及其所有资产租约销毁；租约析构会释放对应 RID。 */
class render_asset_loader final {
public:
  struct asset_candidate {
    render_asset_reload source;
    render_asset_lease lease;
    std::uint64_t update_sequence{};
    std::weak_ptr<const engine::resource::resource_cache::entry> expected;
    std::shared_ptr<const mesh_resource> mesh;
    std::shared_ptr<const material_resource> material;
    std::shared_ptr<const texture_resource> texture;
    std::shared_ptr<const void> previous;
    std::shared_ptr<const engine::resource::material_document_lease> document;
    std::array<std::shared_ptr<const engine::resource::resource_cache::entry>, 5> dependencies;
    std::array<std::shared_ptr<const texture_resource>, 5> dependency_textures;
    std::size_t bytes{};
    std::shared_ptr<const std::vector<std::byte>> texture_payload{};
  };
  /** 所属线程分配请求顺序；仅用于拒绝迟到结果，不是内容版本或缓存失效序号。 */
  [[nodiscard]] std::uint64_t next_update_sequence() noexcept { return ++update_sequence_; }
  [[nodiscard]] bool claim_update(const render_asset_lease& lease, std::uint64_t sequence) noexcept;
  /** profile 非零时校验纹理及材质纹理依赖的设备身份；零值供逻辑层读取已发布 RID。 */
  [[nodiscard]] gneiss_result acquire_cached(const render_asset_reload& source,
                                             render_asset_lease& output,
                                             texture_prepare_profile profile = {}) const noexcept;
  /** 所属线程取得请求目标；同 URI 复用缓存中的 RID，缺失时先建立空内容槽位。
   * 缓存条目存在不表示 CPU/GPU 就绪；失败后的无使用者目标由 release_unused 回收。
   * 只供普通加载使用，调用方仍须等待上传成功再 publish 内容。 */
  [[nodiscard]] gneiss_result reserve_asset(const render_asset_reload& source,
                                            render_asset_lease& output) noexcept;
  /** 为稳定 RID 准备临时上传数据，上传完成后才替换当前内容。 */
  [[nodiscard]] gneiss_result stage_asset(prepared_render_asset prepared,
                                          std::span<const asset_candidate> staged,
                                          asset_candidate& output,
                                          const render_asset_lease& reserved = {},
                                          std::uint64_t sequence = 0U) noexcept;
  [[nodiscard]] gneiss_result publish_assets(std::span<asset_candidate> candidates) noexcept;
  [[nodiscard]] static mesh_asset_lease mesh_lease(const render_asset_lease& lease) noexcept;
  [[nodiscard]] static material_asset_lease
  material_lease(const render_asset_lease& lease) noexcept;
  [[nodiscard]] bool owns(const render_asset_lease& lease) const noexcept;
  /** 保留场景实际持有的资源引用，不查询 URI 缓存；空租约仍为空。 */
  [[nodiscard]] static render_asset_lease asset_lease(const mesh_asset_lease& lease) noexcept;
  [[nodiscard]] static render_asset_lease asset_lease(const material_asset_lease& lease) noexcept;
  /** 返回指定材质持有的纹理租约；非材质返回空数组，不读取来源或缓存。 */
  [[nodiscard]] static std::array<render_asset_lease, 5>
  texture_dependencies(const render_asset_lease& material) noexcept;
  render_asset_loader(const engine::resource::virtual_file_system& file_system,
                      engine::resource::resource_cache& cache,
                      render_resource_service& resources) noexcept;

  /** 宿主装配设备后设置同步加载的纹理准备配置。 */
  void set_default_profile(texture_prepare_profile profile) noexcept { default_profile_ = profile; }
  [[nodiscard]] gneiss_result acquire_mesh(std::string_view uri, mesh_asset_lease& out_lease,
                                           asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss_result acquire_material(std::string_view uri,
                                               material_asset_lease& out_lease,
                                               asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss_result acquire_texture(std::string_view uri, texture_asset_lease& out_lease,
                                              asset_diagnostic& out_diagnostic) noexcept;
  /** 同步复用通用 CPU 准备和发布流程；旧 URI 资源保留 RID，失败保旧。
   * 不等待 GPU 上传；后续渲染帧按 CPU 内容重建镜像。已有使用者保持同一 RID。 */
  [[nodiscard]] gneiss_result reload_assets(std::span<const render_asset_reload> assets,
                                            asset_diagnostic& out_diagnostic) noexcept;
  void release_unused() noexcept { cache_.release_unused(); }

private:
  [[nodiscard]] std::uint64_t* update_sequence(const render_asset_lease& lease) const noexcept;
  std::uint64_t update_sequence_{};
  texture_prepare_profile default_profile_{};
  const engine::resource::virtual_file_system& file_system_;
  engine::resource::resource_cache& cache_;
  render_resource_service& resources_;
};

} // namespace gneiss::engine::render

#endif
