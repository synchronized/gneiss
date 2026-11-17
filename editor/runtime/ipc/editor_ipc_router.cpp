// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/domains.hpp>
#include "runtime/ipc/editor_ipc_router.hpp"
#include <gneiss/engine/error.hpp>

#include <utility>

namespace gneiss::host {

editor_ipc_router::editor_ipc_router() noexcept {
  is_ready_ = router_.bind(gneiss::protocol::domain::session, decode_runtime_session_event) ==
                  gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::control, decode_runtime_control_event) ==
                  gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::log, decode_runtime_log_event) ==
                  gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::inspection,
                           decode_runtime_inspection_event) == gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::statistics,
                           decode_runtime_statistics_event) == gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::property, decode_runtime_property_event) ==
                  gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::asset, decode_runtime_asset_event) ==
                  gneiss::error::success &&
              router_.bind(gneiss::protocol::domain::scene, decode_runtime_scene_event) ==
                  gneiss::error::success;
}

gneiss::error editor_ipc_router::dispatch(
    const gneiss::ipc::envelope& envelope, bool is_authenticated,
    const std::vector<gneiss::protocol::domain_capability>& negotiated_domains,
    runtime_ipc_event& output) {
  if (!is_ready_) {
    return gneiss::error::invalid_state;
  }
  const gneiss::protocol::dispatch_context context{.remote_role =
                                                         gneiss::protocol::peer_role::runtime,
                                                     .handshake_complete = is_authenticated,
                                                     .negotiated_domains = negotiated_domains};
  auto routed = router_.dispatch(envelope, context);
  if (!routed.accepted()) {
    return routed.outcome.rejection == gneiss::protocol::dispatch_rejection::handler_failed
               ? routed.outcome.handler_result
               : gneiss::error::invalid_argument;
  }
  output = std::move(*routed.message);
  return gneiss::error::success;
}

bool editor_ipc_router::is_ready() const noexcept { return is_ready_; }

} // namespace gneiss::host
