// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_LOG_IPC_LOG_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_LOG_IPC_LOG_PROTOCOL_H_

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/protocol/message.hpp>
#include <gneiss/protocol/runtime_log_protocol.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_log_domain_version = 2U;
enum class ipc_log_operation : std::uint16_t { event = 1U };

extern const protocol::message_descriptor<runtime_log_record> log_record;
[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_log_operations() noexcept;

} // namespace gneiss::host

#endif
