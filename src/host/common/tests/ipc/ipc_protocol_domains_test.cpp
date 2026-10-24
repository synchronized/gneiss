// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc_control_protocol.hpp"
#include "ipc_protocol_domains.hpp"

namespace {

gneiss::engine::core::result
accept(void* context, const gneiss::engine::platform::ipc_envelope& /*envelope*/) noexcept {
  ++*static_cast<unsigned*>(context);
  return gneiss::engine::core::result::success;
}

} // namespace

int main() {
  unsigned calls = 0U;
  gneiss::engine::platform::ipc_domain_registry registry;
  if (gneiss::register_ipc_v2_domains(accept, &calls, registry) !=
          gneiss::engine::core::result::success ||
      registry.size() != 8U || gneiss::ipc_v2_domain_capabilities().size() != 7U) {
    return 1;
  }
  gneiss::engine::platform::ipc_envelope request;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 1U, request) !=
      gneiss::engine::core::result::success) {
    return 2;
  }
  const gneiss::engine::platform::ipc_dispatch_context context{
      .remote_role = gneiss::engine::platform::ipc_peer_role::editor,
      .handshake_complete = true,
      .negotiated_domains = gneiss::ipc_v2_domain_capabilities()};
  if (!gneiss::engine::platform::ipc_dispatcher(registry).dispatch(request, context).accepted() ||
      calls != 1U) {
    return 3;
  }
  gneiss::engine::platform::ipc_domain_registry unchanged;
  return gneiss::register_ipc_v2_domains(nullptr, nullptr, unchanged) ==
                     gneiss::engine::core::result::invalid_argument &&
                 unchanged.size() == 0U
             ? 0
             : 4;
}
