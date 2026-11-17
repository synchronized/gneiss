// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_LOG_IPC_LOG_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_LOG_IPC_LOG_PROTOCOL_H_

#include <gneiss/engine/error.hpp>
#include <gneiss/protocol/dispatcher.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_log_domain_version = 1U;
enum class ipc_log_operation : std::uint16_t { event = 1U };

[[nodiscard]] gneiss::error encode_ipc_log_event(std::string_view event,
                                                         gneiss::ipc::envelope& output);
[[nodiscard]] gneiss::error
decode_ipc_log_event(const gneiss::ipc::envelope& envelope, std::string& output);
[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_log_operations() noexcept;

} // namespace gneiss::host

#endif
