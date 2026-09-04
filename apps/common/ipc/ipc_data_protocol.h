// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DATA_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DATA_PROTOCOL_H_

#include "ipc_dispatcher.h"
#include "ipc_inspection_protocol.h"
#include "ipc_property_edit_protocol.h"
#include "ipc_statistics_protocol.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_log_domain_version = 1U;
inline constexpr std::uint16_t ipc_inspection_domain_version = 1U;
inline constexpr std::uint16_t ipc_statistics_domain_version = 1U;
inline constexpr std::uint16_t ipc_property_domain_version = 1U;

enum class ipc_log_operation : std::uint16_t { event = 1U };
enum class ipc_inspection_operation : std::uint16_t { snapshot = 1U, resync = 2U };
enum class ipc_statistics_operation : std::uint16_t { snapshot = 1U };
enum class ipc_property_operation : std::uint16_t { write = 1U };

[[nodiscard]] result encode_ipc_log_event(std::string_view event, ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_log_event(const ipc_envelope& envelope,
                                          std::string& output) noexcept;

[[nodiscard]] result encode_ipc_inspection_batch_v2(const ipc_inspection_batch& batch,
                                                    std::vector<ipc_envelope>& output) noexcept;
[[nodiscard]] result decode_ipc_inspection_batch_v2(const ipc_envelope& envelope,
                                                    ipc_inspection_batch& output) noexcept;
[[nodiscard]] result encode_ipc_inspection_resync(std::uint32_t request_id,
                                                  ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_inspection_resync(const ipc_envelope& envelope) noexcept;

[[nodiscard]] result encode_ipc_statistics_v2(const ipc_runtime_statistics& statistics,
                                              ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_statistics_v2(const ipc_envelope& envelope,
                                              ipc_runtime_statistics& output) noexcept;

[[nodiscard]] result encode_ipc_property_write_v2(const ipc_property_write& command,
                                                  std::uint32_t request_id,
                                                  ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_property_write_v2(const ipc_envelope& envelope,
                                                  ipc_property_write& output) noexcept;
[[nodiscard]] result encode_ipc_property_result_v2(const ipc_property_write_result& response,
                                                   std::uint32_t request_id,
                                                   ipc_envelope& output) noexcept;
[[nodiscard]] result decode_ipc_property_result_v2(const ipc_envelope& envelope,
                                                   ipc_property_write_result& output) noexcept;

[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_log_operations() noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_inspection_operations() noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_statistics_operations() noexcept;
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_property_operations() noexcept;

} // namespace gneiss

#endif
