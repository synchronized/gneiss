// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_ASSET_IPC_ASSET_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_ASSET_IPC_ASSET_PROTOCOL_H_

#include "ipc_dispatcher.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_asset_domain_version = 2U;
inline constexpr std::size_t ipc_asset_max_payload_size = 256U * 1024U;

enum class ipc_asset_operation : std::uint16_t { reload = 1U, resync = 2U };
enum class ipc_asset_type : std::uint8_t { texture, material, static_mesh, scene, prefab };
enum class ipc_asset_apply_status : std::uint8_t {
  applied,
  failed,
  stale,
  restart_required,
};

struct ipc_asset_revision final {
  std::string uri;
  ipc_asset_type type{ipc_asset_type::texture};
};

/** Editor 发往 Runtime 的已提交资产修订；正文由 Runtime 通过工程 VFS 读取。 */
struct ipc_asset_reload_request final {
  std::uint64_t session_id = 0U;
  std::uint64_t revision = 0U;
  std::vector<ipc_asset_revision> assets;
};

/** Runtime 对整个修订事务的权威应用结果。 */
struct ipc_asset_reload_result final {
  std::uint64_t session_id = 0U;
  std::uint64_t revision = 0U;
  ipc_asset_apply_status status{ipc_asset_apply_status::failed};
  std::string message;
};

[[nodiscard]] result encode_ipc_asset_request(const ipc_asset_reload_request& request,
                                              std::vector<std::uint8_t>& output) noexcept;
[[nodiscard]] result decode_ipc_asset_request(std::span<const std::uint8_t> payload,
                                              ipc_asset_reload_request& output) noexcept;
[[nodiscard]] result encode_ipc_asset_result(const ipc_asset_reload_result& response,
                                             std::vector<std::uint8_t>& output) noexcept;
[[nodiscard]] result decode_ipc_asset_result(std::span<const std::uint8_t> payload,
                                             ipc_asset_reload_result& output) noexcept;

[[nodiscard]] result encode_ipc_asset_request_v2(const ipc_asset_reload_request& request,
                                                 ipc_asset_operation operation,
                                                 std::uint32_t request_id,
                                                 ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_asset_request_v2(const ipc_envelope& envelope,
                                                 ipc_asset_reload_request& output) noexcept;
[[nodiscard]] result encode_ipc_asset_result_v2(const ipc_asset_reload_result& response,
                                                ipc_asset_operation operation,
                                                std::uint32_t request_id,
                                                ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_asset_result_v2(const ipc_envelope& envelope,
                                                ipc_asset_reload_result& output) noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_asset_operations() noexcept;

} // namespace gneiss

#endif
