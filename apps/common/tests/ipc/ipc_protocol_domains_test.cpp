// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_control_protocol.h"
#include "ipc_protocol_domains.h"

namespace {

gneiss::result accept(void* context, const gneiss::ipc_envelope& /*envelope*/) noexcept {
  ++*static_cast<unsigned*>(context);
  return gneiss::result::success;
}

} // namespace

int main() {
  unsigned calls = 0U;
  gneiss::ipc_domain_registry registry;
  if (gneiss::register_ipc_v2_domains(accept, &calls, registry) != gneiss::result::success ||
      registry.size() != 7U || gneiss::ipc_v2_domain_capabilities().size() != 6U) {
    return 1;
  }
  gneiss::ipc_envelope request;
  if (gneiss::encode_ipc_control_request(gneiss::ipc_control_operation::pause, 1U, request) !=
      gneiss::result::success) {
    return 2;
  }
  const gneiss::ipc_dispatch_context context{.remote_role = gneiss::ipc_peer_role::editor,
                                             .handshake_complete = true,
                                             .negotiated_domains =
                                                 gneiss::ipc_v2_domain_capabilities()};
  if (!gneiss::ipc_dispatcher(registry).dispatch(request, context).accepted() || calls != 1U) {
    return 3;
  }
  gneiss::ipc_domain_registry unchanged;
  return gneiss::register_ipc_v2_domains(nullptr, nullptr, unchanged) ==
                     gneiss::result::invalid_argument &&
                 unchanged.size() == 0U
             ? 0
             : 4;
}
