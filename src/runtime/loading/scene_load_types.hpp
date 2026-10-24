// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <gneiss/engine/scene.h>
#include <string>
namespace gneiss::runtime {
enum class scene_load_phase : std::uint8_t {
  preparing,
  assets,
  instantiating,
  ready,
  applied,
  failed,
  cancelled
};
struct scene_load_progress {
  std::uint64_t request{};
  std::uint64_t session{};
  std::uint64_t revision{};
  scene_load_phase phase{scene_load_phase::preparing};
  std::size_t completed{};
  std::size_t total{};
  /** 去重后的候选逻辑容量；纹理释放 CPU 负载后仍计入所选负载，约束 2 GiB 上限。 */
  std::size_t resident_bytes{};
  bool can_cancel{};
  bool gpu_in_flight{};
  /** 候选已提交 CPU 数组字节与纹理逻辑负载；不是 RSS 或驱动显存。 */
  std::size_t cpu_data_bytes{};
  std::size_t texture_payload_bytes{};
  std::size_t peak_upload_bytes{};
  /** Application 资源账本快照；不含后台临时分配或驱动额外占用。 */
  std::uint64_t application_logical_bytes{};
  std::uint64_t application_cpu_data_bytes{};
  std::uint64_t available_bytes{};
  std::uint64_t upload_reserved_bytes{};
  /** 失败/取消后请求容器与上传租约已释放；不表示活动场景或旧帧占用归零。 */
  bool cleanup_complete{};
  /** 取消已确认，但上传回执与候选清理仍在推进；此时不得重试。 */
  bool cleanup_pending{};
};
struct scene_load_completion {
  scene_load_progress progress;
  gneiss::engine::core::result result{gneiss::engine::core::result::internal};
  std::string message;
  gneiss_scene_instance scene{};
  double prepare_ms{};
  double asset_prepare_ms{};
  double upload_ms{};
  double maximum_advance_ms{};
  double activation_ms{};
  double cleanup_ms{};
};

struct scene_retirement_statistics {
  double last_ms{};
  std::uint64_t retired_domains{};
  std::size_t live_resources{};
  bool pending{};
};

} // namespace gneiss::runtime
