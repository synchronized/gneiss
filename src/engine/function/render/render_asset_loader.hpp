// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_

#include "engine/core/rid.hpp"
#include "engine/function/render/render_asset_preparation.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/resource/cache/resource_cache.hpp"

#include "engine/core/result.hpp"

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

/** 统一持有资产缓存条目；非空仅表示身份存在，不代表 GPU 就绪。 */
using render_asset_ref = engine::resource::asset_ref<const engine::resource::resource_cache::entry>;
[[nodiscard]] engine::core::rid asset_rid(const render_asset_ref& asset) noexcept;
[[nodiscard]] inline render_asset_type asset_kind(const render_asset_ref& asset) noexcept {
  return asset ? static_cast<render_asset_type>(asset->type) : render_asset_type::invalid;
}
[[nodiscard]] inline std::string_view asset_uri(const render_asset_ref& asset) noexcept {
  return asset ? std::string_view(asset->uri) : std::string_view{};
}

/** 上传任务只携带不可变 CPU 数据；后端所有权留在渲染服务。 */
struct render_upload_item {
  std::shared_ptr<const mesh_resource> mesh;
  std::shared_ptr<const material_resource> material;
  std::shared_ptr<const texture_resource> texture;
  std::array<std::shared_ptr<const texture_resource>, 5> dependency_textures;
  std::size_t bytes{};
  std::shared_ptr<const std::vector<std::byte>> texture_payload{};
};

/** resources 必须晚于缓存及其所有资产租约销毁；租约析构会释放对应 RID。 */
class render_asset_loader final {
public:
  /** 单个加载任务的上传数据；没有独立资产身份或候选 RID。 */
  struct asset_upload {
    render_asset_ref target;
    std::uint64_t sequence{};
    render_upload_item data;
    std::array<gneiss::engine::resource::asset_ref<const engine::resource::resource_cache::entry>,
               5>
        dependencies;
  };
  /** 所属线程分配请求顺序；仅用于拒绝迟到结果，不是内容版本或缓存失效序号。 */
  [[nodiscard]] std::uint64_t next_update_sequence() noexcept;
  [[nodiscard]] bool claim_update(const render_asset_ref& lease, std::uint64_t sequence) noexcept;
  /** profile 非零时校验纹理及材质纹理依赖的设备身份；零值供逻辑层读取已发布 RID。 */
  [[nodiscard]] gneiss::engine::core::result
  acquire_cached(const render_asset_reload& source, render_asset_ref& output,
                 texture_prepare_profile profile = {}) const noexcept;
  /** 所属线程取得请求目标；同 URI 复用缓存中的 RID，缺失时先建立空内容槽位。
   * 缓存条目存在不表示 CPU/GPU 就绪；失败后的无使用者目标由 release_unused 回收。
   * 只供普通加载使用，调用方仍须等待上传成功再更新内容。 */
  [[nodiscard]] gneiss::engine::core::result reserve_asset(const render_asset_reload& source,
                                                           render_asset_ref& output) noexcept;
  /** 临时数据由任务拥有；材质绑定稳定纹理引用，纹理可以尚未就绪。 */
  [[nodiscard]] gneiss::engine::core::result prepare_upload(prepared_render_asset prepared,
                                                            asset_upload& output,
                                                            const render_asset_ref& target = {},
                                                            std::uint64_t sequence = 0U) noexcept;
  /** 上传完成后更新单个资源；拒绝已销毁目标和过期请求，不存在整批提交。 */
  [[nodiscard]] gneiss::engine::core::result complete_upload(asset_upload& upload) noexcept;
  /** 类型筛选：不匹配时返回空引用，不创建新资源。 */
  [[nodiscard]] static render_asset_ref mesh_ref(const render_asset_ref& lease) noexcept;
  [[nodiscard]] static render_asset_ref material_ref(const render_asset_ref& lease) noexcept;
  [[nodiscard]] bool owns(const render_asset_ref& lease) const noexcept;
  /** 返回指定材质持有的纹理租约；非材质返回空数组，不读取来源或缓存。 */
  [[nodiscard]] static std::array<render_asset_ref, 5>
  texture_dependencies(const render_asset_ref& material) noexcept;
  render_asset_loader(const engine::resource::virtual_file_system& file_system,
                      engine::resource::resource_cache& cache,
                      render_resource_service& resources) noexcept;

  /** 宿主装配设备后设置同步加载的纹理准备配置。 */
  void set_default_profile(texture_prepare_profile profile) noexcept { default_profile_ = profile; }
  [[nodiscard]] gneiss::engine::core::result
  acquire_mesh(std::string_view uri, render_asset_ref& out_lease,
               asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss::engine::core::result
  acquire_material(std::string_view uri, render_asset_ref& out_lease,
                   asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss::engine::core::result
  acquire_texture(std::string_view uri, render_asset_ref& out_lease,
                  asset_diagnostic& out_diagnostic) noexcept;
  /** 同步逐项准备并更新 CPU 内容；旧 URI 资源保留 RID，失败保旧。
   * 不等待 GPU 上传；后续渲染帧按 CPU 内容重建镜像。已有使用者保持同一 RID。 */
  [[nodiscard]] gneiss::engine::core::result
  reload_assets(std::span<const render_asset_reload> assets,
                asset_diagnostic& out_diagnostic) noexcept;
  void release_unused() noexcept { cache_.release_unused(); }

private:
  gneiss::engine::core::result prepare_material_upload(prepared_render_asset& prepared,
                                                       asset_upload& upload);
  [[nodiscard]] std::uint64_t* update_sequence(const render_asset_ref& lease) const noexcept;
  texture_prepare_profile default_profile_{};
  const engine::resource::virtual_file_system& file_system_;
  engine::resource::resource_cache& cache_;
  render_resource_service& resources_;
};

} // namespace gneiss::engine::render

#endif
