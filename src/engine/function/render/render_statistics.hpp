// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstddef>
#include <cstdint>

namespace gneiss::engine::render {
/** 渲染队列统计值快照，不暴露执行器或在途任务。 */
struct render_queue_stats final {
  std::size_t pending_tasks{};
  std::size_t pending_frames{};
  std::size_t pending_commands{};
  std::size_t pending_high_watermark{};
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

} // namespace gneiss::engine::render
