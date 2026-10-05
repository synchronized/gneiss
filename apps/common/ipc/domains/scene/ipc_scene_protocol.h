// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "ipc_dispatcher.hpp"

#include <optional>
#include <string>

namespace gneiss {
inline constexpr std::uint16_t ipc_scene_domain_version = 1U;
inline constexpr std::size_t ipc_scene_max_payload_size = 16384U;
enum class ipc_scene_operation : std::uint16_t { load = 1U, cancel = 2U, progress = 3U };
enum class ipc_scene_phase : std::uint8_t {
  preparing,
  assets,
  verifying,
  instantiating,
  ready,
  applied,
  failed,
  cancelled
};
/** 完整场景切换使用独立身份；不能替代资产域的结构热重载。 */
struct ipc_scene_request {
  std::uint64_t session{};
  std::uint64_t revision{};
  std::string uri;
};
/** 可选资源账本快照，单位字节；不是 RSS 或驱动显存，终态保留结束时数值。 */
struct ipc_scene_budget {
  std::uint64_t candidate_logical_bytes{};
  std::uint64_t candidate_cpu_data_bytes{};
  std::uint64_t application_logical_bytes{};
  std::uint64_t application_cpu_data_bytes{};
  std::uint64_t available_bytes{};
  std::uint64_t upload_reserved_bytes{};
  std::uint64_t peak_upload_bytes{};
};
struct ipc_scene_progress {
  ipc_scene_request source;
  ipc_scene_phase phase{ipc_scene_phase::preparing};
  std::uint32_t completed{};
  std::uint32_t total{};
  bool can_cancel{};
  std::string message;
  std::optional<ipc_scene_budget> budget{};
};
[[nodiscard]] bool scene_phase_terminal(ipc_scene_phase phase) noexcept;
[[nodiscard]] result encode_ipc_scene_request(const ipc_scene_request& value,
                                              ipc_scene_operation operation,
                                              std::uint32_t request_id,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_scene_request(const ipc_envelope& envelope,
                                              ipc_scene_request& output) noexcept;
/** 启动场景及进度使用事件；外部切换的唯一终态使用原 request_id 的响应。 */
[[nodiscard]] result encode_ipc_scene_progress(const ipc_scene_progress& value,
                                               std::uint32_t request_id,
                                               ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_scene_progress(const ipc_envelope& envelope,
                                               ipc_scene_progress& output) noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_scene_operations() noexcept;
} // namespace gneiss
