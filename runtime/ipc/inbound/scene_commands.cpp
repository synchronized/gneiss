// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/protocol/domains.hpp>

namespace gneiss::host {
namespace {
error enqueue_scene_command(runtime_command_context& context, protocol::request_context request,
                            ipc_scene_request value, bool cancel) {
  if (context.actions().scene_commands.size() >= 8U)
    return error::not_ready;
  context.actions().scene_commands.push_back(
      {.request = std::move(value), .request_id = request.id, .cancel = cancel});
  return error::success;
}
}
error register_runtime_scene_commands(runtime_command_router& router) noexcept {
  auto status =
      router.bind(scene_load, [](runtime_command_context& context,
                                 protocol::request_context request, ipc_scene_request value) {
        return enqueue_scene_command(context, request, std::move(value), false);
      });
  if (status.failed())
    return status;
  return router.bind(scene_cancel, [](runtime_command_context& context,
                                      protocol::request_context request, ipc_scene_request value) {
    return enqueue_scene_command(context, request, std::move(value), true);
  });
}
}
