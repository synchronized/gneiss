// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.hpp"
#include <gneiss/engine/error.hpp>

#include <gneiss/protocol/ipc_asset_protocol.hpp>

namespace gneiss::host {
error register_runtime_asset_commands(runtime_command_router& router) noexcept {
  auto reload = [](ipc_asset_operation operation) {
    return [operation](runtime_command_context& context, protocol::request_context id,
                       ipc_asset_reload_request value) -> error {
      if (context.actions().asset_reloads.size() >= 8)
        return error::not_ready;
      context.actions().asset_reloads.push_back({std::move(value), operation, id.id});
      return error::success;
    };
  };
  auto status = router.bind(asset_reload, reload(ipc_asset_operation::reload));
  if (status.failed())
    return status;
  status = router.bind(asset_resync, reload(ipc_asset_operation::resync));
  if (status.failed())
    return status;
  return router.bind(asset_cancel,
                     [](runtime_command_context& context, protocol::request_context,
                        ipc_asset_cancel_request value) -> error {
                       if (context.actions().asset_cancels.size() >= 8)
                         return error::not_ready;
                       context.actions().asset_cancels.push_back(
                           {value.session_id, value.revision, {}});
                       return error::success;
                     });
}
}
