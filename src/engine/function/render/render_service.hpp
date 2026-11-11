// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/export.hpp"
#include "engine/core/result.hpp"
#include "engine/function/render/commands.hpp"
#include "engine/function/render/frame_request.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/platform/native_window_info.hpp"
#include "engine/resource/texture/texture_view.hpp"
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gneiss {
struct render_memory_usage {
  std::uint64_t logical_bytes{};
  std::uint64_t cpu_data_bytes{};
};
/** 设备关闭时的存活资源计数；由后端转换，不包含后端句柄。 */
struct render_device_stats {
  std::uint64_t total_live_count{};
  std::uint64_t buffer_count{};
  std::uint64_t texture_count{};
  std::uint64_t texture_view_count{};
  std::uint64_t sampler_count{};
  std::uint64_t shader_count{};
  std::uint64_t bind_group_layout_count{};
  std::uint64_t bind_group_count{};
  std::uint64_t pipeline_layout_count{};
  std::uint64_t graphics_pipeline_count{};
  std::uint64_t compute_pipeline_count{};
  std::uint64_t surface_count{};
  std::uint64_t swapchain_count{};
  std::uint64_t command_recorder_count{};
  std::uint64_t frame_context_count{};
  std::uint64_t frame_count{};
  std::uint64_t timestamp_query_pool_count{};
  std::uint64_t upload_batch_count{};
  std::uint64_t pending_retirement_count{};
  std::uint64_t async_operation_count{};
  std::uint64_t readback_batch_count{};
};

/** 渲染提交与设备生命周期服务；所属线程调用，GPU 操作由内部执行器调度。
 * 借用的日志队列必须覆盖 shutdown；宿主须先关闭本服务，再关闭窗口和平台。
 * 实现固定在堆上，不允许复制或移动。 */
class render_service final {
public:
  static constexpr std::uint64_t default_memory_limit = UINT64_C(4) * 1024U * 1024U * 1024U;
  explicit GNEISS_API render_service(std::uint64_t memory_limit = default_memory_limit);
  GNEISS_API ~render_service();
  render_service(const render_service&) = delete;
  render_service& operator=(const render_service&) = delete;
  render_service(render_service&&) = delete;
  render_service& operator=(render_service&&) = delete;
  [[nodiscard]] GNEISS_API error initialize(const native_window_info& window,
                                            std::span<const std::byte> environment_asset = {},
                                            float environment_intensity = 1.0F,
                                            float environment_rotation_radians = 0.0F);
  /** 异步提交已读取的环境字节；接受后执行器持有引用，调用方不等待 GPU。
   * 队列满返回 not_ready；解析失败沿用内置环境，执行失败由正常帧完成分发返回。 */
  [[nodiscard]] GNEISS_API error request_environment(
      std::shared_ptr<const std::vector<std::byte>> bytes, float intensity, float rotation);
  [[nodiscard]] GNEISS_API error shutdown(render_device_stats& stats);
  [[nodiscard]] GNEISS_API error finish_frames();
  GNEISS_API void set_completion_wakeup(std::shared_ptr<wakeup_signal> notification) noexcept;
  /** 领取必需帧完成记录；空值表示暂无记录，失败表示收集完成结果时出错。 */
  [[nodiscard]] GNEISS_API result<std::optional<render_frame_completion>>
  try_take_required_completion();
  [[nodiscard]] GNEISS_API render_queue_stats query_performance_stats() const noexcept;
  /** initialize 完成后读取；设备重建后重新获取。 */
  [[nodiscard]] GNEISS_API texture_prepare_profile texture_profile() const noexcept;

  /** CPU 持久场景；不需要初始化窗口或 GPU。初始化执行器后命令按序入队；未初始化时同步生效。 */
  [[nodiscard]] GNEISS_API result<rid> create_scene();
  [[nodiscard]] GNEISS_API result<void> submit(render::scene_commands&&);
  [[nodiscard]] GNEISS_API result<void> destroy_scene(rid scene);
  /** 所属线程提交普通帧；内部处理完成回执和背压，not_ready 时保留请求供重试。
   * 成功后消费请求，不代表 GPU 完成；UI 纹理与场景资源固定为不可变版本。
   * 当前支持零或一个窗口视图，零视图只显示 UI。 */
  [[nodiscard]] GNEISS_API result<void> render(render::frame_request&& request);
  /** 异步提交必达截图，返回既有完成回执序号；失败不消费请求。
   * image 必须非空，成功接受后至对应完成回执前不得读写或复用于其他请求。
   * 暂复用 required completion，未来统一为独立回读请求。 */
  [[nodiscard]] GNEISS_API result<std::uint64_t>
  request_capture(render::frame_request&& request, std::shared_ptr<frame_image> image);

  /** 仅关闭 CPU 资源入口；设备关闭由 shutdown 完成。所属线程调用。 */
  GNEISS_API void close_resources() noexcept;
  /** 所属线程查询；跟踪共享对象，包含仍被旧帧持有的已销毁 RID 数据，不强持有对象。 */
  [[nodiscard]] GNEISS_API render_memory_usage memory_usage() const noexcept;
  [[nodiscard]] GNEISS_API std::uint64_t available_memory_bytes() const noexcept;

  [[nodiscard]] GNEISS_API bool is_valid() const noexcept;
  /** 所属线程预留合法 RID；尚无 CPU 内容，不可直接绘制。由 replace_* 填充，destroy_* 取消。
   * 不创建后端对象；所有权属于调用方，销毁和跨服务校验与普通 RID 一致。 */
  [[nodiscard]] GNEISS_API result<rid> reserve_mesh();
  [[nodiscard]] GNEISS_API result<rid> reserve_texture();
  [[nodiscard]] GNEISS_API result<rid> reserve_material();
  /** 只查询 CPU 内容：SUCCESS 已填充，NOT_READY 仅预留，INVALID_HANDLE 已失效或不属于本服务。 */
  [[nodiscard]] GNEISS_API error mesh_status(rid mesh) const noexcept;
  [[nodiscard]] GNEISS_API error texture_status(rid texture) const noexcept;
  [[nodiscard]] GNEISS_API error material_status(rid material) const noexcept;
  [[nodiscard]] GNEISS_API result<rid> create_mesh(const mesh_view& desc);
  /** 校验并接收拥有型网格，成功路径不再复制数组。 */
  [[nodiscard]] GNEISS_API result<rid> create_mesh(mesh_data resource);
  /** 内部 CPU 候选已经校验，移动所有权避免主线程复制大数组。 */
  [[nodiscard]] GNEISS_API result<rid> create_prepared_mesh(mesh_data resource);
  /** 已校验不可变 CPU 网格；RID 与旧帧共享同一对象，销毁 RID 不使外部引用失效。 */
  [[nodiscard]] GNEISS_API result<rid>
  create_prepared_mesh(std::shared_ptr<const mesh_data> resource);
  [[nodiscard]] GNEISS_API result<void> replace_mesh(rid rid,
                                                     std::shared_ptr<const mesh_data> data);
  [[nodiscard]] GNEISS_API result<void> replace_material(rid rid,
                                                         std::shared_ptr<const material_data> data);
  /** 校验并复制借用视图，返回自有 CPU 内容，不分配 RID。 */
  [[nodiscard]] GNEISS_API result<std::shared_ptr<const mesh_data>>
  prepare_mesh_data(const mesh_view& desc);
  [[nodiscard]] GNEISS_API result<std::shared_ptr<const texture_data>>
  prepare_texture_data(const texture_view& desc);
  /** 校验并登记临时 CPU 数据，不分配 RID；任务和帧引用共同决定数据寿命。 */
  [[nodiscard]] GNEISS_API result<std::shared_ptr<const mesh_data>>
  prepare_mesh_data(std::shared_ptr<const mesh_data> resource);
  [[nodiscard]] GNEISS_API result<std::shared_ptr<const material_data>>
  prepare_material_data(const material_data& resource);
  [[nodiscard]] GNEISS_API result<std::shared_ptr<const texture_data>>
  prepare_texture_data(texture_data resource);
  /** 共享原 CPU 缓冲；校验和后端保活不复制像素或负载。 */
  [[nodiscard]] GNEISS_API result<std::shared_ptr<const texture_data>>
  prepare_texture_data(std::shared_ptr<const texture_data> resource);
  [[nodiscard]] GNEISS_API error destroy_mesh(rid mesh) noexcept;
  [[nodiscard]] GNEISS_API result<rid> create_material(const material_data& value);
  [[nodiscard]] GNEISS_API error destroy_material(rid material) noexcept;
  [[nodiscard]] GNEISS_API result<rid> create_texture(const texture_view& desc);
  /** 创建已经过容器校验的内部多 Mip Texture。 */
  [[nodiscard]] GNEISS_API result<rid> create_texture(texture_data resource);
  /** 保存已经过外层容器与 Granit Manifest 检查的运行纹理。 */
  [[nodiscard]] GNEISS_API result<rid> create_packaged_texture(texture_data resource);
  [[nodiscard]] GNEISS_API error destroy_texture(rid texture) noexcept;
  /** 主线程提交已验证候选；保持 RID generation，既有帧仍拥有旧数据快照。 */
  [[nodiscard]] GNEISS_API result<void>
  replace_texture(rid texture, std::shared_ptr<const texture_data> prepared);
  [[nodiscard]] GNEISS_API const mesh_data* get_mesh(rid mesh) const noexcept;
  [[nodiscard]] GNEISS_API const material_data* get_material(rid material) const noexcept;
  [[nodiscard]] GNEISS_API const texture_data* get_texture(rid texture) const noexcept;
  [[nodiscard]] GNEISS_API std::shared_ptr<const mesh_data> share_mesh(rid mesh) const noexcept;
  [[nodiscard]] GNEISS_API std::shared_ptr<const material_data>
  share_material(rid material) const noexcept;
  [[nodiscard]] GNEISS_API std::shared_ptr<const texture_data>
  share_texture(rid texture) const noexcept;
  [[nodiscard]] GNEISS_API std::size_t live_resource_count() const noexcept;

private:
  result<void> dispatch_render_work(std::function<void()>);
  result<std::uint64_t> submit_request(render::frame_request&, render_frame_policy,
                                       std::shared_ptr<frame_image>);
  struct resource_storage;
  template <typename Resource>
  result<std::shared_ptr<const Resource>>
  track(std::shared_ptr<const Resource> resource,
        std::vector<std::weak_ptr<const Resource>>& history);
  // 后端先析构，再释放 CPU 内容表；上层资源端点由宿主提前关闭。
  std::unique_ptr<resource_storage> storage_;
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
} // namespace gneiss
