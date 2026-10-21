// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_HPP_
#define GNEISS_SRC_APPLICATION_APPLICATION_ASSET_RELOAD_INTERNAL_HPP_

#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_instance_service.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/scene.h>

#include <span>
#include <string_view>

namespace gneiss::runtime {
using mesh_binding = engine::scene::scene_instance_service::mesh_binding;
/** 所属线程从就绪租约创建节点；失败输出无效句柄，不读取文件。 */
GNEISS_API gneiss_result
create_mesh_node_ready(gneiss_application application, gneiss_scene_instance scene,
                       const engine::scene::mesh_renderer_node_creation& desc,
                       const mesh_binding& prepared, gneiss_scene_node_id& output) noexcept;
/** 所属线程释放无使用者候选；调用前先释放完成回执和临时租约。 */
GNEISS_API gneiss_result collect_unused_edit_assets(gneiss_application application) noexcept;
/** 同步调用 accept 提交编辑；回调失败不采用候选版本，不得重入资产服务。 */
GNEISS_API gneiss_result accept_edit_assets(
    gneiss_application application, const engine::render::texture_load_completion& completion,
    const std::function<gneiss_result()>& accept) noexcept;
/** 内部编辑接口：只读就绪选择、捕获及提交实际租约；不加载文件。 */
GNEISS_API gneiss_result query_ready_asset(gneiss_application application,
                                           const engine::render::render_asset_reload& source,
                                           engine::render::render_asset_lease& output) noexcept;
GNEISS_API gneiss_result capture_mesh_binding(gneiss_application application,
                                              gneiss_scene_instance scene,
                                              gneiss_scene_node_id node,
                                              mesh_binding& output) noexcept;
GNEISS_API gneiss_result replace_mesh_binding(gneiss_application application,
                                              gneiss_scene_instance scene,
                                              gneiss_scene_node_id node,
                                              const mesh_binding& expected,
                                              const mesh_binding& replacement) noexcept;
/** 只准备固定版本候选，不修改使用者或默认选择；回执通过 poll_textures 取得。 */
GNEISS_API gneiss_result prepare_edit_assets(
    gneiss_application application, std::span<const engine::render::render_asset_reload> sources,
    std::uint64_t revision, std::uint64_t& request) noexcept;

/** 内部同步诊断入口：通过相同场景管线离屏渲染并回读 GPU；不是逐帧截图接口。
 * 仅所属线程，宽高 1..1024，等待渲染完成；不用于性能采样。 */
GNEISS_API gneiss_result capture_frame(gneiss_application application, std::uint32_t width,
                                       std::uint32_t height,
                                       engine::render::frame_image& output) noexcept;

/** 内部测量入口，返回已完成渲染帧的统计，不以主循环 tick 代替实际渲染。 */
GNEISS_API gneiss_result query_render_statistics(
    gneiss_application application, engine::render::render_queue_stats& output) noexcept;

/** 借用宿主执行入口直到 Application 关闭；同一宿主不再创建第二个池。 */
GNEISS_API gneiss_result attach_task_executor(
    gneiss_application application, engine::core::tasks::task_executor& executor) noexcept;
/** 所属线程接受混合资产批次；CPU 解析交给宿主执行器。
 * reload=true 使用固定版本，自动扩展活动 World 中引用请求纹理的材质；上传确认后
 * poll_textures 在帧快照捕获前批量切换场景租约和 ECS。失败保留原引用。
 * 裸 RID 不跟随更新；reload=false 保留 URI 缓存加载行为，不切换现有场景。 */
GNEISS_API gneiss_result request_render_assets(
    gneiss_application application, std::span<const engine::render::render_asset_reload> assets,
    std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
    bool reload = true) noexcept;
GNEISS_API gneiss_result query_asset_load_progress(gneiss_application application,
                                                   engine::render::asset_load_progress& progress,
                                                   bool& active) noexcept;
/** 提交许可前接受取消；已进入 GPU 阶段或没有在途批次时返回 NOT_READY。 */
GNEISS_API gneiss_result cancel_render_assets(gneiss_application application) noexcept;
GNEISS_API gneiss_result request_textures(gneiss_application application,
                                          std::span<const std::string> uris, std::uint64_t session,
                                          std::uint64_t revision, std::uint64_t& request,
                                          bool reload = true) noexcept;
/** 所属线程在 update 或运行循环外调用；成功回执表示场景引用切换已完成。
 * 回执租约须早于 Application 释放；纹理请求可能附带受影响材质的完成结果。 */
GNEISS_API gneiss_result poll_textures(gneiss_application application,
                                       engine::render::texture_load_completion& completion,
                                       bool& ready) noexcept;
GNEISS_API gneiss_result cancel_textures(gneiss_application application) noexcept;

/** 在 Application 主线程原子重载一组渲染资产；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result
reload_render_assets(gneiss_application application,
                     std::span<const engine::render::render_asset_reload> assets) noexcept;

/** 在 Application 主线程事务式重载现有 Scene 实例；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result reload_scene(gneiss_application application,
                                      gneiss_scene_instance instance,
                                      std::string_view uri) noexcept;

/** 刷新 Scene 内指定 URI 的全部 Prefab 实例；仅供 Gneiss 宿主使用。 */
GNEISS_API gneiss_result reload_prefab(gneiss_application application,
                                       gneiss_scene_instance instance,
                                       std::string_view uri) noexcept;

} // namespace gneiss::runtime

#endif
