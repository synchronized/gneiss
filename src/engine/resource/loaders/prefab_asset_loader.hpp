// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_ASSET_LOADER_HPP_
#define GNEISS_SCENE_PREFAB_ASSET_LOADER_HPP_

#include "engine/core/result.hpp"
#include "engine/resource/asset_ref.hpp"
#include "engine/resource/types/prefab_description.hpp"

#include <memory>
#include <string_view>

namespace gneiss::engine {
class asset_manager;
}

namespace gneiss::engine {

using prefab_asset_ref = asset_ref<const prefab_description>;

class prefab_asset_loader final {
public:
  explicit prefab_asset_loader(asset_manager& assets) noexcept : assets_(assets) {}
  [[nodiscard]] asset_manager& assets() const noexcept { return assets_; }

  [[nodiscard]] gneiss::engine::result acquire(std::string_view uri, prefab_asset_ref& out_lease,
                                               scene_diagnostic& out_diagnostic) noexcept;
  [[nodiscard]] gneiss::engine::result reload(std::string_view uri, prefab_asset_ref& out_lease,
                                              scene_diagnostic& out_diagnostic) noexcept;
  /** 所属线程将已解析描述放入 URI 缓存，不读文件；旧实例引用仍保持有效。
   * 租约可独立于加载器存活，失败不替换已有缓存。 */
  [[nodiscard]] gneiss::engine::result install_prepared(std::string_view uri,
                                                        prefab_description description,
                                                        prefab_asset_ref& out_lease) noexcept;

private:
  [[nodiscard]] gneiss::engine::result load(std::string_view uri, prefab_asset_ref& output,
                                            scene_diagnostic& diagnostic, bool reload) noexcept;
  asset_manager& assets_;
};

} // namespace gneiss::engine

#endif
