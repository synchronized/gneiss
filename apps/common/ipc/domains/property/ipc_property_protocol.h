// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_PROPERTY_IPC_PROPERTY_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_PROPERTY_IPC_PROPERTY_PROTOCOL_H_

#include "ipc_inspection_protocol.h"

#include "ipc_dispatcher.h"

#include <gneiss/reflection.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gneiss {

inline constexpr std::uint16_t ipc_property_domain_version = 1U;
enum class ipc_property_operation : std::uint16_t { write = 1U };

using ipc_property_payload =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
                 std::array<std::uint8_t, 16>, std::array<float, 3>, std::array<float, 4>>;

/** IPC 自有的类型化属性值；字符串和数组不借用调用方内存。 */
struct ipc_property_value final {
  ipc_property_payload payload;
};

/** Editor 发往 Runtime 的单字段写入命令。 */
struct ipc_property_write final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  ipc_runtime_object_id object;
  gneiss_type_id type_id{};
  gneiss_field_id field_id = GNEISS_NULL_FIELD_ID;
  std::uint64_t expected_revision = 0U;
  ipc_property_value value;
};

/** Runtime 返回的权威执行结果；成功时携带规范值和新修订号。 */
struct ipc_property_write_result final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  std::int32_t code = 0;
  std::uint64_t revision = 0U;
  std::string message;
  ipc_property_value canonical_value;
};

[[nodiscard]] result encode_ipc_property_write(const ipc_property_write& command,
                                               std::vector<std::uint8_t>& output) noexcept;
[[nodiscard]] result decode_ipc_property_write(std::span<const std::uint8_t> payload,
                                               ipc_property_write& output) noexcept;
[[nodiscard]] result encode_ipc_property_write_result(const ipc_property_write_result& response,
                                                      std::vector<std::uint8_t>& output) noexcept;
[[nodiscard]] result decode_ipc_property_write_result(std::span<const std::uint8_t> payload,
                                                      ipc_property_write_result& output) noexcept;

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
[[nodiscard]] std::span<const ipc_operation_descriptor> ipc_property_operations() noexcept;

} // namespace gneiss

#endif
