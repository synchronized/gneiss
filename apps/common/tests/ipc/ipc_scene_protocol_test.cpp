// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_protocol_domains.h"
#include "ipc_scene_protocol.h"

#include <cstdio>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("场景协议契约失败，行=" + std::to_string(location.line()));
}
void run() {
  const ipc_scene_request source{5U, 7U, "asset://scenes/main.scene.json"};
  ipc_envelope envelope;
  ipc_scene_request request;
  check(encode_ipc_scene_request(source, ipc_scene_operation::load, 9U, envelope) ==
        result::success);
  check(decode_ipc_scene_request(envelope, request) == result::success);
  check(request.session == source.session && request.revision == source.revision &&
        request.uri == source.uri);
  check(encode_ipc_scene_request(source, ipc_scene_operation::load, 0U, envelope) ==
        result::invalid_argument);
  check(encode_ipc_scene_request(source, ipc_scene_operation::cancel, 0U, envelope) ==
        result::success);
  check(decode_ipc_scene_request(envelope, request) == result::success);
  envelope.kind = ipc_message_kind::request;
  check(decode_ipc_scene_request(envelope, request) == result::invalid_argument);
  auto invalid = source;
  invalid.uri = "asset://../escape";
  check(encode_ipc_scene_request(invalid, ipc_scene_operation::load, 9U, envelope) ==
        result::invalid_argument);
  for (unsigned index = 0U; index < 8U; ++index) {
    const auto phase = static_cast<ipc_scene_phase>(index);
    ipc_scene_progress progress{.source = source,
                                .phase = phase,
                                .completed = 512U,
                                .total = 65536U,
                                .can_cancel = !scene_phase_terminal(phase),
                                .message = "阶段信息"};
    check(encode_ipc_scene_progress(progress, 9U, envelope) == result::success);
    check(envelope.kind ==
          (scene_phase_terminal(phase) ? ipc_message_kind::response : ipc_message_kind::event));
    ipc_scene_progress decoded;
    check(decode_ipc_scene_progress(envelope, decoded) == result::success);
    check(decoded.phase == phase && decoded.total == progress.total &&
          decoded.source.uri == source.uri);
    check(encode_ipc_scene_progress(progress, 0U, envelope) == result::success &&
          envelope.kind == ipc_message_kind::event);
    check(decode_ipc_scene_progress(envelope, decoded) == result::success);
    progress.total = 65537U;
    check(encode_ipc_scene_progress(progress, 9U, envelope) == result::invalid_argument);
  }
  ipc_scene_progress invalid_progress{
      .source = source, .phase = ipc_scene_phase::applied, .can_cancel = true, .message = {}};
  check(encode_ipc_scene_progress(invalid_progress, 9U, envelope) == result::invalid_argument);
  envelope.payload.assign(ipc_scene_max_payload_size + 1U, 0U);
  check(decode_ipc_scene_progress(envelope, invalid_progress) == result::invalid_argument);
}
}
int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
