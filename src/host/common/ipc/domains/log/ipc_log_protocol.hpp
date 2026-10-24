// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_LOG_IPC_LOG_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_LOG_IPC_LOG_PROTOCOL_H_

#include "engine/core/result.hpp"
#include "ipc_dispatcher.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace gneiss {

inline constexpr std::uint16_t ipc_log_domain_version = 1U;
enum class ipc_log_operation : std::uint16_t { event = 1U };

[[nodiscard]] gneiss::engine::core::result
encode_ipc_log_event(std::string_view event,
                     gneiss::engine::platform::ipc_envelope& output) noexcept;
[[nodiscard]] gneiss::engine::core::result
decode_ipc_log_event(const gneiss::engine::platform::ipc_envelope& envelope,
                     std::string& output) noexcept;
[[nodiscard]] std::span<const gneiss::engine::platform::ipc_operation_descriptor>
ipc_log_operations() noexcept;

} // namespace gneiss

#endif
