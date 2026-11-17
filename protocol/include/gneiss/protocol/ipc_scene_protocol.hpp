// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/message.hpp>

#include <optional>
#include <string>

namespace gneiss::host {
inline constexpr std::uint16_t ipc_scene_domain_version = 2U;
inline constexpr std::size_t ipc_scene_max_payload_size = 16384U;
enum class ipc_scene_operation : std::uint16_t { load = 1U, cancel = 2U, progress = 3U };
enum class ipc_scene_phase : std::uint8_t {
  preparing,
  assets,
  instantiating,
  ready,
  applied,
  failed,
  cancelled
};
/** 完整场景切换使用独立身份；不能替代资产域的结构热重载。 */
struct ipc_scene_request {
  static constexpr auto type_guid = guid::literal("d0c30f11-28c8-4ffc-9559-3b4f06cf868f");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_scene_request";
  std::uint64_t session{};
  std::uint64_t revision{};
  std::string uri;
};
/** 可选资源账本快照，单位字节；不是 RSS 或驱动显存，终态保留结束时数值。 */
struct ipc_scene_budget {
  static constexpr auto type_guid = guid::literal("3302d701-0eb1-4e77-af5e-b81259d28290");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_scene_budget";
  std::uint64_t candidate_logical_bytes{};
  std::uint64_t candidate_cpu_data_bytes{};
  std::uint64_t application_logical_bytes{};
  std::uint64_t application_cpu_data_bytes{};
  std::uint64_t available_bytes{};
  /** true 表示失败/取消请求已完成清理；不能与 cleanup_pending 同时为 true。 */
  bool cleanup_complete{};
  bool cleanup_pending{};
};
struct ipc_scene_progress {
  static constexpr auto type_guid = guid::literal("c1072a1e-8858-44cc-a7f9-8ec1650b90b8");
  static constexpr std::string_view type_name = "gneiss.protocol.ipc_scene_progress";
  ipc_scene_request source;
  ipc_scene_phase phase{ipc_scene_phase::preparing};
  std::uint32_t completed{};
  std::uint32_t total{};
  bool can_cancel{};
  std::string message;
  std::optional<ipc_scene_budget> budget{};
};
/** 同一负载的不同消息语义由描述区分，不复制操作码到负载。 */
extern const protocol::message_descriptor<ipc_scene_request> scene_load;
extern const protocol::message_descriptor<ipc_scene_request> scene_cancel;
extern const protocol::message_descriptor<ipc_scene_progress> scene_progress;
extern const protocol::message_descriptor<ipc_scene_progress> scene_result;
/** 仅补报同一已取消请求的清理账本，不改变终态或请求身份。 */
[[nodiscard]] bool apply_scene_cleanup_update(ipc_scene_progress& current,
                                              const ipc_scene_progress& incoming) noexcept;
[[nodiscard]] bool scene_phase_terminal(ipc_scene_phase phase) noexcept;
[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_scene_operations() noexcept;
} // namespace gneiss::host
