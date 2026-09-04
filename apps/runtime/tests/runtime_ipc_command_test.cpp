// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc/runtime_commands.h"

#include "ipc_data_protocol.h"

#include <array>
#include <vector>

namespace {

struct sent_envelopes final {
  std::vector<gneiss::ipc_envelope> values;
};

gneiss::result capture_send(void* context, gneiss::ipc_envelope envelope) noexcept {
  try {
    static_cast<sent_envelopes*>(context)->values.push_back(std::move(envelope));
    return gneiss::result::success;
  } catch (...) {
    return gneiss::result::out_of_memory;
  }
}

} // namespace

int main() {
  using namespace gneiss;
  using namespace gneiss::runtime_internal;

  runtime_command_router router;
  if (register_runtime_commands(router) != result::success) {
    return 1;
  }
  constexpr std::array domains{
      ipc_domain_capability{.domain = ipc_domain::session, .version = 1U},
      ipc_domain_capability{.domain = ipc_domain::control, .version = 1U},
      ipc_domain_capability{.domain = ipc_domain::inspection, .version = 1U},
      ipc_domain_capability{.domain = ipc_domain::property, .version = 1U}};
  const ipc_dispatch_context dispatch_context{.remote_role = ipc_peer_role::editor,
                                              .handshake_complete = true,
                                              .negotiated_domains = domains};
  runtime_ipc_state state = runtime_ipc_state::running;
  runtime_ipc_actions actions;
  sent_envelopes sent;
  runtime_command_context context(state, actions, &sent, capture_send);

  gneiss::ipc_envelope envelope;
  if (encode_ipc_control_request(ipc_control_operation::pause, 7U, envelope) != result::success ||
      !router.dispatch(envelope, dispatch_context, context).accepted() || !actions.pause_game ||
      state != runtime_ipc_state::paused || sent.values.empty()) {
    return 2;
  }
  const ipc_property_write property{.session_id = 3U,
                                    .command_id = 9U,
                                    .object = {4U, 2U},
                                    .type_id = {{1U}},
                                    .field_id = 5U,
                                    .expected_revision = 6U,
                                    .value = {true}};
  if (encode_ipc_property_write_v2(property, 9U, envelope) != result::success ||
      !router.dispatch(envelope, dispatch_context, context).accepted() ||
      actions.property_writes.size() != 1U || actions.property_writes.front().command_id != 9U) {
    return 3;
  }
  return 0;
}
