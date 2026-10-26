// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/result.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/platform/native_window_info.hpp"
#include <memory>
#include <span>
#include <vector>

namespace gneiss::engine {
class log_dispatcher;
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
  render_service();
  /** 内部装配入口：借用服务所拥有的 CPU 资源表，不能独立关闭或延长其寿命。 */
  [[nodiscard]] render_resource_service& resources() noexcept;
  ~render_service();
  render_service(const render_service&) = delete;
  render_service& operator=(const render_service&) = delete;
  render_service(render_service&&) = delete;
  render_service& operator=(render_service&&) = delete;
  [[nodiscard]] result initialize(const native_window_info& window,
                                  std::span<const std::byte> environment_asset = {},
                                  float environment_intensity = 1.0F,
                                  float environment_rotation_radians = 0.0F,
                                  log_dispatcher* log = nullptr) noexcept;
  [[nodiscard]] result shutdown(render_device_stats& stats) noexcept;
  [[nodiscard]] result finish_frames() noexcept;
  void set_completion_notification(std::shared_ptr<progress_notification> notification) noexcept;
  [[nodiscard]] result prepare_frame_packet_storage(render_frame_packet& packet,
                                                    bool& out_should_prepare) noexcept;
  [[nodiscard]] result submit(render_frame_packet packet,
                              render_frame_policy policy = render_frame_policy::replaceable,
                              std::uint64_t* out_sequence = nullptr) noexcept;
  [[nodiscard]] bool try_take_required_completion(render_frame_completion& completion) noexcept;
  [[nodiscard]] render_queue_stats query_performance_stats() const noexcept;
  /** initialize 完成后读取；设备重建后重新获取。 */
  [[nodiscard]] texture_prepare_profile texture_profile() const noexcept;

private:
  struct implementation;
  std::unique_ptr<implementation> impl_;
};
} // namespace gneiss::engine
