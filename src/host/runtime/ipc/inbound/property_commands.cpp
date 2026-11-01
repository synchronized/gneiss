// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "ipc/runtime_commands.hpp"

#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include "ipc_property_protocol.hpp"

namespace gneiss::host {
namespace {

constexpr std::size_t runtime_property_write_budget = 32U;

gneiss::engine::error handle_property_write(const gneiss::engine::ipc_envelope& envelope,
                                            runtime_command_context& context) noexcept {
  ipc_property_write command;
  const auto decoded = decode_ipc_property_write_v2(envelope, command);
  if (decoded != gneiss::engine::error::success) {
    return decoded;
  }
  if (context.actions().property_writes.size() >= runtime_property_write_budget) {
    const ipc_property_write_result response{.session_id = command.session_id,
                                             .command_id = command.command_id,
                                             .code = gneiss::engine::error::not_ready.code(),
                                             .revision = 0U,
                                             .message = "本帧属性写入队列已满",
                                             .canonical_value = {}};
    gneiss::engine::ipc_envelope response_envelope;
    const auto encoded =
        make_property_write_result_event(response, envelope.request_id, response_envelope);
    return encoded == gneiss::engine::error::success ? context.send(std::move(response_envelope))
                                                     : encoded;
  }
  context.actions().property_writes.push_back(std::move(command));
  return gneiss::engine::error::success;
}

} // namespace

gneiss::engine::error register_runtime_property_commands(runtime_command_router& router) noexcept {
  return router.bind(gneiss::engine::ipc_domain::property,
                     static_cast<std::uint16_t>(ipc_property_operation::write),
                     handle_property_write);
}

} // namespace gneiss::host
