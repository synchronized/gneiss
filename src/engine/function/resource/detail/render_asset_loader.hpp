// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_
#define GNEISS_RENDER_RENDER_ASSET_LOADER_HPP_

#include "engine/core/rid.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/resource/asset_preparation.hpp"
#include "engine/function/resource/asset_types.hpp"
#include "engine/function/resource/detail/render_resource_factory.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"
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

class render_service;

/** Loader 的资源服务必须存活；资源引用可晚于服务释放，关闭后仅析构无后端依赖的外壳。 */
class render_asset_loader final {
public:
  /** 单个加载任务的 CPU 内容；没有独立资产身份或候选 RID。 */
  using asset_update = render_asset_update;
  /** 所属线程分配请求顺序；仅用于拒绝迟到结果，不是内容版本或缓存失效序号。 */
  [[nodiscard]] std::uint64_t next_update_sequence() noexcept;
  [[nodiscard]] bool claim_update(const render_asset_ref& lease, std::uint64_t sequence) noexcept;
  /** profile 非零时校验纹理及材质纹理依赖的设备身份；零值供逻辑层读取已发布 RID。 */
  [[nodiscard]] result acquire_cached(const render_asset_reload& source, render_asset_ref& output,
                                      texture_prepare_profile profile = {}) const noexcept;
  /** 所属线程取得请求目标；同 URI 复用缓存中的 RID，缺失时先建立空内容槽位。
   * 缓存条目存在不表示 CPU/GPU 就绪；失败后的无使用者目标由 release_unused 回收。
   * 只供普通加载使用，CPU 解码成功后采用内容。 */
  [[nodiscard]] result reserve_asset(const render_asset_reload& source,
                                     render_asset_ref& output) noexcept;
  /** 临时数据由任务拥有；材质绑定稳定纹理引用，纹理可以尚未就绪。 */
  [[nodiscard]] result prepare_update(prepared_render_asset prepared, asset_update& output,
                                      const render_asset_ref& target = {},
                                      std::uint64_t sequence = 0U) noexcept;
  /** CPU 解码完成后更新单个资源；拒绝已销毁目标和过期请求，不存在整批提交。 */
  [[nodiscard]] result apply_update(asset_update& upload) noexcept;
  /** 类型筛选：不匹配时返回空引用，不创建新资源。 */
  [[nodiscard]] static mesh mesh_ref(const render_asset_ref& lease) noexcept;
  [[nodiscard]] static material material_ref(const render_asset_ref& lease) noexcept;
  [[nodiscard]] bool owns(const mesh& value) const noexcept;
  [[nodiscard]] bool owns(const texture& value) const noexcept;
  [[nodiscard]] bool owns(const material& value) const noexcept;
  [[nodiscard]] bool owns(const render_asset_ref& lease) const noexcept;
  render_asset_loader(asset_manager& assets, render_resource_factory& factory) noexcept;

  [[nodiscard]] asset_manager& assets() const noexcept { return assets_; }

  /** 宿主装配设备后设置缓存查询使用的纹理设备配置。 */
  void set_default_profile(texture_prepare_profile profile) noexcept { default_profile_ = profile; }
  [[nodiscard]] result acquire_mesh(std::string_view uri, engine::mesh& out_lease,
                                    asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] result acquire_material(std::string_view uri, engine::material& out_lease,
                                        asset_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] result acquire_texture(std::string_view uri, engine::texture& out_lease,
                                       asset_diagnostic& out_diagnostic) noexcept;
  /** 只查询缓存，不读盘；缺失返回 not_found，由调用方请求异步加载。 */
  [[nodiscard]] result acquire_asset(render_asset_type type, std::string_view uri,
                                     render_asset_ref& output,
                                     asset_diagnostic& diagnostic) noexcept;
  void release_unused() noexcept;

private:
  result prepare_material_update(prepared_render_asset& prepared, asset_update& upload);
  [[nodiscard]] std::uint64_t update_sequence(const render_asset_ref& lease) const noexcept;
  asset_manager& assets_;
  texture_prepare_profile default_profile_{};
  resource_cache& cache_;
  render_service& resources_;
  render_resource_factory& factory_;
};

} // namespace gneiss::engine

#endif
