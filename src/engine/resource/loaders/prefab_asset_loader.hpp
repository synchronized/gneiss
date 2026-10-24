// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_ASSET_LOADER_HPP_
#define GNEISS_SCENE_PREFAB_ASSET_LOADER_HPP_

#include "engine/core/result.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/types/prefab_description.hpp"

#include <memory>
#include <string_view>

namespace gneiss::engine::resource {
class virtual_file_system;
}

namespace gneiss::engine::resource {

using prefab_asset_ref = asset_ref<const prefab_description>;

class prefab_asset_loader final {
public:
  prefab_asset_loader(const engine::resource::virtual_file_system& file_system,
                      engine::resource::resource_cache& cache) noexcept
      : file_system_(file_system), cache_(cache) {}

  [[nodiscard]] gneiss::engine::core::result acquire(std::string_view uri,
                                                     prefab_asset_ref& out_lease,
                                                     scene_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss::engine::core::result reload(std::string_view uri,
                                                    prefab_asset_ref& out_lease,
                                                    scene_diagnostic& out_diagnostic) noexcept;
  /** 所属线程将已解析描述放入 URI 缓存，不读文件；旧实例引用仍保持有效。
   * 租约可独立于加载器存活，失败不替换已有缓存。 */
  [[nodiscard]] gneiss::engine::core::result install_prepared(std::string_view uri,
                                                              prefab_description description,
                                                              prefab_asset_ref& out_lease) noexcept;

private:
  const engine::resource::virtual_file_system& file_system_;
  engine::resource::resource_cache& cache_;
};

} // namespace gneiss::engine::resource

#endif
