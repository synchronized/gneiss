// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_HPP_
#define GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_HPP_

#include "engine/core/result.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_instances.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/scene.h>

#include <span>
#include <string_view>

namespace gneiss::runtime {
using mesh_binding = gneiss::engine::scene_instances::mesh_binding;

/** 内部编辑接口：只读就绪选择和捕获实际租约；不加载文件。 */
GNEISS_API gneiss::engine::result
query_ready_asset(gneiss_application application, const gneiss::engine::render_asset_reload& source,
                  gneiss::engine::render_asset_ref& output) noexcept;
GNEISS_API gneiss::engine::result capture_mesh_binding(gneiss_application application,
                                                       gneiss_scene_instance scene,
                                                       gneiss_scene_node_id node,
                                                       mesh_binding& output) noexcept;

/** 加载编辑所需资源，不提交作者修改；回执通过 poll_textures 取得。 */
GNEISS_API gneiss::engine::result
prepare_edit_assets(gneiss_application application,
                    std::span<const gneiss::engine::render_asset_reload> sources,
                    std::uint64_t revision, std::uint64_t& request) noexcept;

/** 内部同步诊断入口：通过相同场景管线离屏渲染并回读 GPU；不是逐帧截图接口。
 * 仅所属线程，宽高 1..1024，等待渲染完成；不用于性能采样。 */
GNEISS_API gneiss::engine::result capture_frame(gneiss_application application, std::uint32_t width,
                                                std::uint32_t height,
                                                gneiss::engine::frame_image& output) noexcept;

/** 内部测量入口，返回已完成渲染帧的统计，不以主循环 tick 代替实际渲染。 */
GNEISS_API gneiss::engine::result
query_render_statistics(gneiss_application application,
                        gneiss::engine::render_queue_stats& output) noexcept;

/** 借用宿主执行入口直到 Application 关闭；同一宿主不再创建第二个池。 */
GNEISS_API gneiss::engine::result
attach_task_executor(gneiss_application application,
                     gneiss::engine::task_executor& executor) noexcept;
/** 所属线程接受混合资产批次；CPU 解析交给宿主执行器。
 * 上传确认后逐项更新同一 RID 的内容，不重绑 ECS 或自动重读依赖它的材质。
 * reload=false 可复用已有内容；失败/取消不撤回已经生效的项。 */
GNEISS_API gneiss::engine::result
request_render_assets(gneiss_application application,
                      std::span<const gneiss::engine::render_asset_reload> assets,
                      std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                      bool reload = true) noexcept;

/** 提交许可前接受取消；已进入 GPU 阶段或没有在途批次时返回 NOT_READY。 */
GNEISS_API gneiss::engine::result cancel_render_assets(gneiss_application application) noexcept;
GNEISS_API gneiss::engine::result request_textures(gneiss_application application,
                                                   std::span<const std::string> uris,
                                                   std::uint64_t session, std::uint64_t revision,
                                                   std::uint64_t& request,
                                                   bool reload = true) noexcept;
/** 所属线程在 update 或运行循环外调用；回执报告资源内容更新结果。
 * 回执租约须早于 Application 释放；逐项结果包含已发现依赖，纹理更新不附带材质重读。 */
GNEISS_API gneiss::engine::result poll_textures(gneiss_application application,
                                                gneiss::engine::texture_load_completion& completion,
                                                bool& ready) noexcept;
GNEISS_API gneiss::engine::result cancel_textures(gneiss_application application) noexcept;

/** 主线程同步准备并发布旧 URI 缓存资源，保留其 RID；失败保留旧内容。
 * 不等待 GPU，后续帧重建镜像；不重绑场景引用。仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss::engine::result
reload_render_assets(gneiss_application application,
                     std::span<const gneiss::engine::render_asset_reload> assets) noexcept;

/** 在 Application 主线程事务式重载现有 Scene 实例；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss::engine::result reload_scene(gneiss_application application,
                                               gneiss_scene_instance instance,
                                               std::string_view uri) noexcept;

/** 刷新 Scene 内指定 URI 的全部 Prefab 实例；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss::engine::result reload_prefab(gneiss_application application,
                                                gneiss_scene_instance instance,
                                                std::string_view uri) noexcept;

} // namespace gneiss::runtime

#endif
