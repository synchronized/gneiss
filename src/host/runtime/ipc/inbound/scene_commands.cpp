// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc/runtime_commands.hpp"

namespace gneiss::host {
namespace {
gneiss::engine::result handle_scene_command(const gneiss::engine::ipc_envelope& envelope,
                                            runtime_command_context& context) noexcept try {
  ipc_scene_request request;
  const auto decoded = decode_ipc_scene_request(envelope, request);
  if (decoded != gneiss::engine::result::success) {
    return decoded;
  }
  if (context.actions().scene_commands.size() >= 8U) {
    return gneiss::engine::result::not_ready;
  }
  context.actions().scene_commands.push_back({
      .request = std::move(request),
      .request_id = envelope.request_id,
      .cancel = envelope.operation == static_cast<std::uint16_t>(ipc_scene_operation::cancel),
  });
  return gneiss::engine::result::success;
} catch (...) {
  return gneiss::engine::result::out_of_memory;
}
}
gneiss::engine::result register_runtime_scene_commands(runtime_command_router& router) noexcept {
  auto operation = router.bind(gneiss::engine::ipc_domain::scene, 1U, handle_scene_command);
  if (operation == gneiss::engine::result::success) {
    operation = router.bind(gneiss::engine::ipc_domain::scene, 2U, handle_scene_command);
  }
  return operation;
}
} // namespace gneiss::host
