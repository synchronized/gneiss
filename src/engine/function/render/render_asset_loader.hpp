// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_

#include "engine/core/rid.hpp"
#include "engine/function/render/render_asset_preparation.hpp"
#include "engine/function/render/render_resource.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/resource/resource_cache.hpp"

#include "engine/core/result.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::engine {
class asset_manager;
}

namespace gneiss::engine {

class render_resource_service;

/** 统一持有资产缓存条目；非空仅表示身份存在，不代表 GPU 就绪。 */
using render_asset_ref = gneiss::engine::asset_ref<const gneiss::engine::resource_cache::entry>;
/** 混合加载结果转为类型化引用；空值和类型不符均返回空。 */
template <typename T>
[[nodiscard]] asset_ref<const T> asset_cast(const render_asset_ref& asset) noexcept {
  return asset ? asset->as<T>() : asset_ref<const T>{};
}
[[nodiscard]] gneiss::engine::rid asset_rid(const render_asset_ref& asset) noexcept;
[[nodiscard]] GNEISS_API render_asset_type asset_kind(const render_asset_ref& asset) noexcept;
[[nodiscard]] inline std::string_view asset_uri(const render_asset_ref& asset) noexcept {
  return asset ? std::string_view(asset->uri) : std::string_view{};
}

inline std::string_view asset_uri(const mesh& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const mesh& value) noexcept { return resource_handle(value); }
inline render_asset_type asset_kind(const mesh& value) noexcept {
  return value ? render_asset_type::mesh : render_asset_type::invalid;
}
inline std::string_view asset_uri(const texture& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const texture& value) noexcept { return resource_handle(value); }
inline render_asset_type asset_kind(const texture& value) noexcept {
  return value ? render_asset_type::texture : render_asset_type::invalid;
}
inline std::string_view asset_uri(const material& value) noexcept {
  return value ? std::string_view(value->source().uri) : std::string_view{};
}
inline rid asset_rid(const material& value) noexcept { return resource_handle(value); }
inline render_asset_type asset_kind(const material& value) noexcept {
  return value ? render_asset_type::material : render_asset_type::invalid;
}

/** 更新任务只携带不可变 CPU 数据；后端所有权留在渲染服务。 */
struct render_resource_content {
  std::shared_ptr<const mesh_data> mesh;
  std::shared_ptr<const material_data> material;
  std::shared_ptr<const texture_data> texture;
  std::size_t bytes{};
};

/** Loader 的资源服务必须存活；资源引用可晚于服务释放，关闭后仅析构无后端依赖的外壳。 */
class render_asset_loader final {
public:
  /** 单个加载任务的 CPU 内容；没有独立资产身份或候选 RID。 */
  struct asset_update {
    render_asset_ref target;
    std::uint64_t sequence{};
    render_resource_content data;
    std::array<texture, 5> dependencies;
  };
  /** 所属线程分配请求顺序；仅用于拒绝迟到结果，不是内容版本或缓存失效序号。 */
  [[nodiscard]] std::uint64_t next_update_sequence() noexcept;
  [[nodiscard]] bool claim_update(const render_asset_ref& lease, std::uint64_t sequence) noexcept;
  /** profile 非零时校验纹理及材质纹理依赖的设备身份；零值供逻辑层读取已发布 RID。 */
  [[nodiscard]] gneiss::engine::result
  acquire_cached(const render_asset_reload& source, render_asset_ref& output,
                 texture_prepare_profile profile = {}) const noexcept;
  /** 所属线程取得请求目标；同 URI 复用缓存中的 RID，缺失时先建立空内容槽位。
   * 缓存条目存在不表示 CPU/GPU 就绪；失败后的无使用者目标由 release_unused 回收。
   * 只供普通加载使用，CPU 解码成功后采用内容。 */
  [[nodiscard]] gneiss::engine::result reserve_asset(const render_asset_reload& source,
                                                     render_asset_ref& output) noexcept;
  /** 临时数据由任务拥有；材质绑定稳定纹理引用，纹理可以尚未就绪。 */
  [[nodiscard]] gneiss::engine::result prepare_update(prepared_render_asset prepared,
                                                      asset_update& output,
                                                      const render_asset_ref& target = {},
                                                      std::uint64_t sequence = 0U) noexcept;
  /** CPU 解码完成后更新单个资源；拒绝已销毁目标和过期请求，不存在整批提交。 */
  [[nodiscard]] gneiss::engine::result apply_update(asset_update& upload) noexcept;
  /** 类型筛选：不匹配时返回空引用，不创建新资源。 */
  [[nodiscard]] static mesh mesh_ref(const render_asset_ref& lease) noexcept;
  [[nodiscard]] static material material_ref(const render_asset_ref& lease) noexcept;
  [[nodiscard]] bool owns(const mesh& value) const noexcept;
  [[nodiscard]] bool owns(const texture& value) const noexcept;
  [[nodiscard]] bool owns(const material& value) const noexcept;
  [[nodiscard]] bool owns(const render_asset_ref& lease) const noexcept;
  render_asset_loader(asset_manager& assets, render_resource_service& resources) noexcept;

  [[nodiscard]] asset_manager& assets() const noexcept { return assets_; }

  /** 宿主装配设备后设置缓存查询使用的纹理设备配置。 */
  void set_default_profile(texture_prepare_profile profile) noexcept { default_profile_ = profile; }
  [[nodiscard]] gneiss::engine::result acquire_mesh(std::string_view uri,
                                                    gneiss::engine::mesh& out_lease,
                                                    asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss::engine::result acquire_material(std::string_view uri,
                                                        gneiss::engine::material& out_lease,
                                                        asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss::engine::result acquire_texture(std::string_view uri,
                                                       gneiss::engine::texture& out_lease,
                                                       asset_diagnostic& out_diagnostic) noexcept;
  /** 只查询缓存，不读盘；缺失返回 not_found，由调用方请求异步加载。 */
  [[nodiscard]] result acquire_asset(render_asset_type type, std::string_view uri,
                                     render_asset_ref& output,
                                     asset_diagnostic& diagnostic) noexcept;
  void release_unused() noexcept;

private:
  gneiss::engine::result prepare_material_update(prepared_render_asset& prepared,
                                                 asset_update& upload);
  [[nodiscard]] std::uint64_t* update_sequence(const render_asset_ref& lease) const noexcept;
  asset_manager& assets_;
  texture_prepare_profile default_profile_{};
  gneiss::engine::resource_cache& cache_;
  render_resource_service& resources_;
};

} // namespace gneiss::engine

#endif
