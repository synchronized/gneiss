// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.h"

#include "ipc_asset_protocol.h"

namespace gneiss::runtime_internal {
namespace {

constexpr std::size_t asset_reload_budget = 8U;

result handle_asset_reload(const ipc_envelope& envelope,
                           runtime_command_context& context) noexcept {
  ipc_asset_reload_request request;
  const auto decoded = decode_ipc_asset_request_v2(envelope, request);
  if (decoded != result::success) {
    return decoded;
  }
  if (context.actions().asset_reloads.size() >= asset_reload_budget) {
    return result::not_ready;
  }
  context.actions().asset_reloads.push_back(
      {.request = std::move(request),
       .operation = static_cast<ipc_asset_operation>(envelope.operation),
       .request_id = envelope.request_id});
  return result::success;
}

} // namespace

result register_runtime_asset_commands(runtime_command_router& router) noexcept {
  auto operation =
      router.bind(ipc_domain::asset, static_cast<std::uint16_t>(ipc_asset_operation::reload),
                  handle_asset_reload);
  if (operation == result::success) {
    operation =
        router.bind(ipc_domain::asset, static_cast<std::uint16_t>(ipc_asset_operation::resync),
                    handle_asset_reload);
  }
  return operation;
}

} // namespace gneiss::runtime_internal
