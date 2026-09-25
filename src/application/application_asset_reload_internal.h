// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_H_
#define GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_H_

#include "asset/texture_load_service.h"
#include "render/render_asset_loader.h"
#include "render/render_executor.h"

#include <gneiss/application.h>
#include <gneiss/scene.h>

#include <span>
#include <string_view>

namespace gneiss::application_internal {

/** 内部测量入口，返回已完成渲染帧的统计，不以主循环 tick 代替实际渲染。 */
GNEISS_API gneiss_result query_render_statistics(
    gneiss_application application, render_internal::render_queue_stats& output) noexcept;

/** 借用宿主执行入口直到 Application 关闭；同一宿主不再创建第二个池。 */
GNEISS_API gneiss_result attach_task_executor(gneiss_application application,
                                              tasks::task_executor& executor) noexcept;
GNEISS_API gneiss_result request_textures(gneiss_application application,
                                          std::span<const std::string> uris, std::uint64_t session,
                                          std::uint64_t revision, std::uint64_t& request,
                                          bool reload = true) noexcept;
GNEISS_API gneiss_result poll_textures(gneiss_application application,
                                       asset_internal::texture_load_completion& completion,
                                       bool& ready) noexcept;
GNEISS_API gneiss_result cancel_textures(gneiss_application application) noexcept;

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
