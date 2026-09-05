// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_H_
#define GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_H_

#include "render/render_asset_loader.h"

#include <gneiss/application.h>
#include <gneiss/scene.h>

#include <span>
#include <string_view>

namespace gneiss::application_internal {

/** 在 Application 主线程原子重载一组渲染资产；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result
reload_render_assets(gneiss_application application,
                     std::span<const render_internal::render_asset_reload> assets) noexcept;

/** 在 Application 主线程事务式重载现有 Scene 实例；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result reload_scene(gneiss_application application,
                                      gneiss_scene_instance instance,
                                      std::string_view uri) noexcept;

/** 刷新 Scene 内指定 URI 的全部 Prefab 实例；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result reload_prefab(gneiss_application application,
                                       gneiss_scene_instance instance,
                                       std::string_view uri) noexcept;

} // namespace gneiss::application_internal

#endif
