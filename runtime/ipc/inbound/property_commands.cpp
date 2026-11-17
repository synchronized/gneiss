// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.hpp"
#include <gneiss/engine/error.hpp>

#include "ipc/outbound/runtime_ipc_outbound.hpp"

#include <gneiss/protocol/ipc_property_protocol.hpp>

namespace gneiss::host {
namespace {

constexpr std::size_t runtime_property_write_budget = 32U;

gneiss::error handle_property_write(runtime_command_context& context,
                                    protocol::request_context request, ipc_property_write command) {
  command.command_id = request.id;
  if (context.actions().property_writes.size() >= runtime_property_write_budget) {
    const ipc_property_write_result response{.session_id = command.session_id,
                                             .command_id = command.command_id,
                                             .code = gneiss::error::not_ready.code(),
                                             .revision = 0U,
                                             .message = "本帧属性写入队列已满",
                                             .canonical_value = {}};
    auto encoded = protocol::encode(property_result, response, request.id);
    return encoded ? context.send(std::move(*encoded)) : encoded.error().error;
  }
  context.actions().property_writes.push_back(std::move(command));
  return gneiss::error::success;
}

} // namespace

gneiss::error register_runtime_property_commands(runtime_command_router& router) noexcept {
  return router.bind(property_write, handle_property_write);
}

} // namespace gneiss::host
