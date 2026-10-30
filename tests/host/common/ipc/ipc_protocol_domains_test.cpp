// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "ipc_control_protocol.hpp"
#include "ipc_protocol_domains.hpp"

using namespace gneiss::host;

namespace {

gneiss::engine::error accept(void* context,
                             const gneiss::engine::ipc_envelope& /*envelope*/) noexcept {
  ++*static_cast<unsigned*>(context);
  return gneiss::engine::error::success;
}

} // namespace

int main() {
  unsigned calls = 0U;
  gneiss::engine::ipc_domain_registry registry;
  if (gneiss::host::register_ipc_v2_domains(accept, &calls, registry) !=
          gneiss::engine::error::success ||
      registry.size() != 8U || gneiss::host::ipc_v2_domain_capabilities().size() != 7U) {
    return 1;
  }
  gneiss::engine::ipc_envelope request;
  if (gneiss::host::encode_ipc_control_request(gneiss::host::ipc_control_operation::pause, 1U,
                                               request) != gneiss::engine::error::success) {
    return 2;
  }
  const gneiss::engine::ipc_dispatch_context context{
      .remote_role = gneiss::engine::ipc_peer_role::editor,
      .handshake_complete = true,
      .negotiated_domains = gneiss::host::ipc_v2_domain_capabilities()};
  if (!gneiss::engine::ipc_dispatcher(registry).dispatch(request, context).accepted() ||
      calls != 1U) {
    return 3;
  }
  gneiss::engine::ipc_domain_registry unchanged;
  return gneiss::host::register_ipc_v2_domains(nullptr, nullptr, unchanged) ==
                     gneiss::engine::error::invalid_argument &&
                 unchanged.size() == 0U
             ? 0
             : 4;
}
