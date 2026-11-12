// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstddef>
#include <cstdint>

namespace gneiss::render {
/** 单帧执行结果；后端填写绘制指标，执行器补充排队与线程耗时。 */
struct execution_result final {
  bool needs_recreate{};
  bool presented{};
  float frame_capture_ms{};
  std::size_t copied_payload_bytes{};
  float queue_wait_ms{};
  float resource_prepare_ms{};
  float acquire_wait_ms{};
  float record_submit_ms{};
  float present_wait_ms{};
  float render_thread_ms{};
  bool gpu_timing_supported{};
  bool gpu_timing_valid{};
  std::uint64_t gpu_timing_sample_sequence{};
  float gpu_frame_ms{};
  float gpu_shadow_ms{};
  float gpu_opaque_ms{};
  float gpu_tone_mapping_ms{};
  bool environment_asset_requested{};
  bool environment_fallback{};
  float environment_intensity{1.0F};
  float environment_rotation_radians{};
};

/** 设备关闭时的存活资源计数；由后端转换，不包含后端句柄。 */
struct device_stats {
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

/** 渲染队列统计值快照，不暴露执行器或在途任务。 */
struct queue_stats final {
  std::size_t pending_tasks{};
  std::size_t pending_frames{};
  std::size_t pending_commands{};
  std::size_t pending_high_watermark{};
  std::size_t command_payload_bytes{};
  std::size_t command_payload_high_watermark{};
  std::size_t frame_input_bytes{};
  std::size_t frame_input_high_watermark{};
  std::uint64_t submitted_frames{};
  std::uint64_t executed_frames{};
  std::uint64_t presented_frames{};
  double latest_frame_interval_ms{};
  double maximum_frame_interval_ms{};
  std::uint64_t replaced_frames{};
  std::uint64_t submitted_required_frames{};
  std::uint64_t executed_required_frames{};
  std::uint64_t rejected_required_frames{};
  std::uint64_t skipped_frame_builds{};
  std::uint64_t submitted_commands{};
  std::uint64_t executed_commands{};
  std::uint64_t rejected_commands{};
  float latest_frame_queue_wait_ms{};
  float maximum_frame_queue_wait_ms{};
  float latest_frame_capture_ms{};
  std::size_t latest_copied_payload_bytes{};
  bool gpu_timing_supported{};
  bool latest_gpu_timing_valid{};
  std::uint64_t latest_gpu_timing_sample_sequence{};
  std::uint64_t gpu_timing_sample_count{};
  std::uint64_t gpu_timing_unavailable_count{};
  float latest_gpu_frame_ms{};
  float latest_gpu_shadow_ms{};
  float latest_gpu_opaque_ms{};
  float latest_gpu_tone_mapping_ms{};
  bool environment_asset_requested{};
  bool environment_fallback{};
  float environment_intensity{1.0F};
  float environment_rotation_radians{};
};

} // namespace gneiss::render
