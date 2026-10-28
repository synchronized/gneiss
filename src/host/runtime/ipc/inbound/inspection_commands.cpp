// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "ipc/runtime_commands.hpp"

#include "ipc_inspection_protocol.hpp"

namespace gneiss::host {
namespace {

gneiss::engine::error handle_resync(const gneiss::engine::ipc_envelope& envelope,
                                    runtime_command_context& context) noexcept {
  const auto operation = decode_ipc_inspection_resync(envelope);
  if (operation == gneiss::engine::error::success) {
    context.actions().request_inspection_resync = true;
  }
  return operation;
}

} // namespace

gneiss::engine::error
register_runtime_inspection_commands(runtime_command_router& router) noexcept {
  return router.bind(gneiss::engine::ipc_domain::inspection,
                     static_cast<std::uint16_t>(ipc_inspection_operation::resync), handle_resync);
}

} // namespace gneiss::host
