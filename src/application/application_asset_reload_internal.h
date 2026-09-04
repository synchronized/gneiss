// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_H_
#define GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_H_

#include "render/render_asset_loader.h"

#include <gneiss/application.h>

#include <span>

namespace gneiss::application_internal {

/** 在 Application 主线程原子重载一组渲染资产；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result
reload_render_assets(gneiss_application application,
                     std::span<const render_internal::render_asset_reload> assets) noexcept;

} // namespace gneiss::application_internal

#endif
