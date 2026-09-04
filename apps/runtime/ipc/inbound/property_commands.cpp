// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.h"

#include "ipc/outbound/runtime_ipc_outbound.h"

#include "ipc_data_protocol.h"

namespace gneiss::runtime_internal {
namespace {

constexpr std::size_t runtime_property_write_budget = 32U;

result handle_property_write(const ipc_envelope& envelope,
                             runtime_command_context& context) noexcept {
  ipc_property_write command;
  const auto decoded = decode_ipc_property_write_v2(envelope, command);
  if (decoded != result::success) {
    return decoded;
  }
  if (context.actions().property_writes.size() >= runtime_property_write_budget) {
    const ipc_property_write_result response{.session_id = command.session_id,
                                             .command_id = command.command_id,
                                             .code = GNEISS_ERROR_NOT_READY,
                                             .revision = 0U,
                                             .message = "本帧属性写入队列已满",
                                             .canonical_value = {}};
    ipc_envelope response_envelope;
    const auto encoded =
        make_property_write_result_event(response, envelope.request_id, response_envelope);
    return encoded == result::success ? context.send(std::move(response_envelope)) : encoded;
  }
  context.actions().property_writes.push_back(std::move(command));
  return result::success;
}

} // namespace

result register_runtime_property_commands(runtime_command_router& router) noexcept {
  return router.bind(ipc_domain::property,
                     static_cast<std::uint16_t>(ipc_property_operation::write),
                     handle_property_write);
}

} // namespace gneiss::runtime_internal
