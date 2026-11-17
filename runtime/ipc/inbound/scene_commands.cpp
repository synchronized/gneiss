// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/domains.hpp>
#include <gneiss/engine/error.hpp>
#include "ipc/runtime_commands.hpp"

namespace gneiss::host {
namespace {
gneiss::error handle_scene_command(const gneiss::ipc::envelope& envelope,
                                           runtime_command_context& context) {
  ipc_scene_request request;
  const auto decoded = decode_ipc_scene_request(envelope, request);
  if (decoded != gneiss::error::success) {
    return decoded;
  }
  if (context.actions().scene_commands.size() >= 8U) {
    return gneiss::error::not_ready;
  }
  context.actions().scene_commands.push_back({
      .request = std::move(request),
      .request_id = envelope.request_id,
      .cancel = envelope.operation == static_cast<std::uint16_t>(ipc_scene_operation::cancel),
  });
  return gneiss::error::success;
}
}
gneiss::error register_runtime_scene_commands(runtime_command_router& router) noexcept {
  auto operation = router.bind(gneiss::protocol::domain::scene, 1U, handle_scene_command);
  if (operation == gneiss::error::success) {
    operation = router.bind(gneiss::protocol::domain::scene, 2U, handle_scene_command);
  }
  return operation;
}
} // namespace gneiss::host
