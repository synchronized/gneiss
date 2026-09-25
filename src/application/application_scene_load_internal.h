// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "application/scene_load_service.h"

#include <gneiss/application.h>

namespace gneiss::application_internal {

/** 旧域回收独立计时；live_resources 为 CPU Service RID 数量，不等于 GPU 驱动驻留量。 */
GNEISS_API gneiss_result query_scene_retirement(gneiss_application application,
                                                scene_retirement_statistics& output) noexcept;

/** 内部完整场景切换；所属线程，要求已绑定宿主执行器。与结构/资产热重载互斥。 */
GNEISS_API gneiss_result request_scene_load(gneiss_application application, std::string_view uri,
                                            std::uint64_t session, std::uint64_t revision,
                                            std::uint64_t& request) noexcept;
GNEISS_API gneiss_result query_scene_load_progress(gneiss_application application,
                                                   scene_load_progress& progress,
                                                   bool& active) noexcept;
/** 推进一次主线程预算，取失败/取消终态；ready 由 activate_scene_load 显式接收。 */
GNEISS_API gneiss_result poll_scene_load(gneiss_application application,
                                         scene_load_completion& completion, bool& ready) noexcept;
GNEISS_API gneiss_result cancel_scene_load(gneiss_application application,
                                           std::uint64_t request) noexcept;
/** ready 后在宿主停止旧模块更新的安全点调用。成功后旧 Scene/World 不再是活动域。
 * 只交换域及对应资产服务；模块回调不属于引擎事务，宿主须重建 Context 与检查镜像。 */
GNEISS_API gneiss_result activate_scene_load(gneiss_application application, std::uint64_t request,
                                             scene_load_completion& completion) noexcept;

} // namespace gneiss::application_internal
