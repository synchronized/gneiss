// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_ASSET_LOADER_H_
#define GNEISS_SCENE_PREFAB_ASSET_LOADER_H_

#include "asset/resource_cache.h"
#include "scene/prefab_description.h"

#include <memory>
#include <string_view>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::scene_internal {

class prefab_asset_lease final {
public:
  [[nodiscard]] const prefab_description* get() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }

private:
  friend class prefab_asset_loader;
  std::shared_ptr<const asset_internal::resource_cache::entry> entry_;
};

class prefab_asset_loader final {
public:
  prefab_asset_loader(const asset_internal::virtual_file_system& file_system,
                      asset_internal::resource_cache& cache) noexcept
      : file_system_(file_system), cache_(cache) {}

  [[nodiscard]] gneiss_result acquire(std::string_view uri, prefab_asset_lease& out_lease,
                                      scene_diagnostic& out_diagnostic) noexcept;

private:
  const asset_internal::virtual_file_system& file_system_;
  asset_internal::resource_cache& cache_;
};

} // namespace gneiss::scene_internal

#endif
