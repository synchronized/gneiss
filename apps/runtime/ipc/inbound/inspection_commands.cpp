// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.h"

#include "ipc_data_protocol.h"

namespace gneiss::runtime_internal {
namespace {

result handle_resync(const ipc_envelope& envelope, runtime_command_context& context) noexcept {
  const auto operation = decode_ipc_inspection_resync(envelope);
  if (operation == result::success) {
    context.actions().request_inspection_resync = true;
  }
  return operation;
}

} // namespace

result register_runtime_inspection_commands(runtime_command_router& router) noexcept {
  return router.bind(ipc_domain::inspection,
                     static_cast<std::uint16_t>(ipc_inspection_operation::resync), handle_resync);
}

} // namespace gneiss::runtime_internal
