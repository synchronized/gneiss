// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_DOMAINS_PROPERTY_IPC_PROPERTY_PROTOCOL_H_
#define GNEISS_APPS_COMMON_IPC_DOMAINS_PROPERTY_IPC_PROPERTY_PROTOCOL_H_

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/protocol/ipc_inspection_protocol.hpp>

#include <gneiss/protocol/message.hpp>

#include <gneiss/engine/core/data/value.hpp>
#include <gneiss/engine/core/base/guid.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gneiss::host {

inline constexpr std::uint16_t ipc_property_domain_version = 2U;
enum class ipc_property_operation : std::uint16_t { write = 1U };

/** Editor 发往 Runtime 的单字段写入命令。 */
struct ipc_property_write final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  ipc_runtime_object_id object;
  gneiss::guid type_id{};
  std::uint32_t field_id = 0U;
  std::uint64_t expected_revision = 0U;
  gneiss::value value;
};

/** Runtime 返回的权威执行结果；成功时携带规范值和新修订号。 */
struct ipc_property_write_result final {
  std::uint64_t session_id = 0U;
  std::uint64_t command_id = 0U;
  std::int32_t code = 0;
  std::uint64_t revision = 0U;
  std::string message;
  gneiss::value canonical_value;
};

extern const protocol::message_descriptor<ipc_property_write> property_write;
extern const protocol::message_descriptor<ipc_property_write_result> property_result;

[[nodiscard]] std::span<const gneiss::protocol::operation_descriptor>
ipc_property_operations() noexcept;

} // namespace gneiss::host

#endif
