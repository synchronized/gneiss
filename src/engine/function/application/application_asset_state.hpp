// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/asset/resource_cache.hpp"
#include "engine/asset/virtual_file_system.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"

namespace gneiss::application_internal {
/** Application 的资产装配与唯一所有者。场景域仅借用；先排空请求并释放场景，再销毁本对象。
 * 渲染资源服务必须更晚销毁；底层缓存不依赖场景或渲染类型。 */
class application_asset_state final {
public:
  application_asset_state(const asset_internal::virtual_file_system& files,
                          render_internal::render_resource_service& resources)
      : files(files), assets(this->files, cache, resources), prefabs(this->files, cache) {}
  asset_internal::virtual_file_system files;
  asset_internal::resource_cache cache;
  render_internal::render_asset_loader assets;
  scene_internal::prefab_asset_loader prefabs;
};
}
