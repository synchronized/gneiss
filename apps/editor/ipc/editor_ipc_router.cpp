// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ipc_router.h"

#include <utility>

namespace gneiss::editor {

editor_ipc_router::editor_ipc_router() noexcept {
  is_ready_ =
      router_.bind(ipc_domain::session, decode_runtime_session_event) == result::success &&
      router_.bind(ipc_domain::control, decode_runtime_control_event) == result::success &&
      router_.bind(ipc_domain::log, decode_runtime_log_event) == result::success &&
      router_.bind(ipc_domain::inspection, decode_runtime_inspection_event) == result::success &&
      router_.bind(ipc_domain::statistics, decode_runtime_statistics_event) == result::success &&
      router_.bind(ipc_domain::property, decode_runtime_property_event) == result::success;
}

result editor_ipc_router::dispatch(const ipc_envelope& envelope, bool is_authenticated,
                                   const std::vector<ipc_domain_capability>& negotiated_domains,
                                   runtime_ipc_event& output) noexcept {
  if (!is_ready_) {
    return result::invalid_state;
  }
  const ipc_dispatch_context context{.remote_role = ipc_peer_role::runtime,
                                     .handshake_complete = is_authenticated,
                                     .negotiated_domains = negotiated_domains};
  auto routed = router_.dispatch(envelope, context);
  if (!routed.accepted()) {
    return routed.outcome.rejection == ipc_dispatch_rejection::handler_failed
               ? routed.outcome.handler_result
               : result::invalid_argument;
  }
  output = std::move(*routed.message);
  return result::success;
}

bool editor_ipc_router::is_ready() const noexcept { return is_ready_; }

} // namespace gneiss::editor
