// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc/runtime_commands.hpp"

#include "ipc_asset_protocol.hpp"

namespace gneiss::host::runtime {
namespace {

constexpr std::size_t asset_reload_budget = 8U;

gneiss::engine::core::result
handle_asset_cancel(const gneiss::engine::platform::ipc_envelope& envelope,
                    runtime_command_context& context) noexcept {
  ipc_asset_reload_request request;
  const auto decoded = decode_ipc_asset_cancel(envelope, request);
  if (decoded != gneiss::engine::core::result::success)
    return decoded;
  if (context.actions().asset_cancels.size() >= asset_reload_budget)
    return gneiss::engine::core::result::not_ready;
  context.actions().asset_cancels.push_back(std::move(request));
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result
handle_asset_reload(const gneiss::engine::platform::ipc_envelope& envelope,
                    runtime_command_context& context) noexcept {
  ipc_asset_reload_request request;
  const auto decoded = decode_ipc_asset_request_v2(envelope, request);
  if (decoded != gneiss::engine::core::result::success) {
    return decoded;
  }
  if (context.actions().asset_reloads.size() >= asset_reload_budget) {
    return gneiss::engine::core::result::not_ready;
  }
  context.actions().asset_reloads.push_back(
      {.request = std::move(request),
       .operation = static_cast<ipc_asset_operation>(envelope.operation),
       .request_id = envelope.request_id});
  return gneiss::engine::core::result::success;
}

} // namespace

gneiss::engine::core::result
register_runtime_asset_commands(runtime_command_router& router) noexcept {
  auto operation =
      router.bind(gneiss::engine::platform::ipc_domain::asset,
                  static_cast<std::uint16_t>(ipc_asset_operation::reload), handle_asset_reload);
  if (operation == gneiss::engine::core::result::success) {
    operation =
        router.bind(gneiss::engine::platform::ipc_domain::asset,
                    static_cast<std::uint16_t>(ipc_asset_operation::resync), handle_asset_reload);
  }
  if (operation == gneiss::engine::core::result::success)
    operation =
        router.bind(gneiss::engine::platform::ipc_domain::asset,
                    static_cast<std::uint16_t>(ipc_asset_operation::cancel), handle_asset_cancel);
  return operation;
}

} // namespace gneiss::host::runtime
