// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_protocol_domains.hpp"
#include "ipc_scene_protocol.hpp"

#include <cstdio>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("场景协议契约失败，行=" + std::to_string(location.line()));
  }
}
void cleanup_updates() {
  const ipc_scene_request source{
      .session = 5U, .revision = 7U, .uri = "asset://scenes/main.scene.json"};
  ipc_envelope envelope;
  ipc_scene_progress invalid_progress;
  ipc_scene_progress draining{.source = source, .phase = ipc_scene_phase::cancelled, .message = {}};
  draining.budget.emplace();
  draining.budget->cleanup_pending = true;
  check(encode_ipc_scene_progress(draining, 9U, envelope) == result::success);
  check(decode_ipc_scene_progress(envelope, invalid_progress) == result::success &&
        invalid_progress.budget && invalid_progress.budget->cleanup_pending);
  auto cleaned = draining;
  cleaned.budget->cleanup_pending = false;
  cleaned.budget->cleanup_complete = true;
  cleaned.message = "资源清理完成";
  for (unsigned mismatch = 0; mismatch < 4; ++mismatch) {
    auto stale = cleaned;
    if (mismatch == 0) {
      ++stale.source.session;
    }
    if (mismatch == 1) {
      ++stale.source.revision;
    }
    if (mismatch == 2) {
      stale.source.uri += ".other";
    }
    if (mismatch == 3) {
      stale.phase = ipc_scene_phase::applied;
    }
    check(!apply_scene_cleanup_update(draining, stale));
    check(draining.budget->cleanup_pending);
  }
  check(encode_ipc_scene_progress(cleaned, 0U, envelope) == result::success &&
        envelope.kind == ipc_message_kind::event);
  auto terminal = draining;
  check(apply_scene_cleanup_update(terminal, cleaned));
  check(terminal.phase == ipc_scene_phase::cancelled && terminal.message == draining.message &&
        terminal.budget->cleanup_complete && !terminal.budget->cleanup_pending);
  check(!apply_scene_cleanup_update(terminal, cleaned));
  draining.budget->cleanup_complete = true;
  check(encode_ipc_scene_progress(draining, 9U, envelope) == result::invalid_argument);
}
void run() {
  cleanup_updates();
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
    check(!decoded.budget.has_value());
    progress.budget = ipc_scene_budget{
        .candidate_logical_bytes = UINT64_MAX,
        .candidate_cpu_data_bytes = 2U,
        .application_logical_bytes = 3U,
        .application_cpu_data_bytes = 4U,
        .available_bytes = 5U,
        .upload_reserved_bytes = 6U,
        .peak_upload_bytes = 7U,
        .cleanup_complete = true,
    };
    check(encode_ipc_scene_progress(progress, 0U, envelope) == result::success);
    check(decode_ipc_scene_progress(envelope, decoded) == result::success);
    check(decoded.budget && decoded.budget->candidate_logical_bytes == UINT64_MAX &&
          decoded.budget->candidate_cpu_data_bytes == 2U &&
          decoded.budget->application_logical_bytes == 3U &&
          decoded.budget->application_cpu_data_bytes == 4U &&
          decoded.budget->available_bytes == 5U && decoded.budget->upload_reserved_bytes == 6U &&
          decoded.budget->peak_upload_bytes == 7U && decoded.budget->cleanup_complete);
    const std::string encoded(envelope.payload.begin(), envelope.payload.end());
    auto legacy = encoded;
    const auto cleanup_offset = legacy.find(",\"cleanup_complete\":true");
    check(cleanup_offset != std::string::npos);
    legacy.erase(cleanup_offset, std::string_view{",\"cleanup_complete\":true"}.size());
    envelope.payload.assign(legacy.begin(), legacy.end());
    check(decode_ipc_scene_progress(envelope, decoded) == result::success && decoded.budget &&
          !decoded.budget->cleanup_complete);
    const auto pending_offset = legacy.find(",\"cleanup_pending\":false");
    check(pending_offset != std::string::npos);
    legacy.erase(pending_offset, std::string_view{",\"cleanup_pending\":false"}.size());
    envelope.payload.assign(legacy.begin(), legacy.end());
    check(decode_ipc_scene_progress(envelope, decoded) == result::success && decoded.budget &&
          !decoded.budget->cleanup_complete && !decoded.budget->cleanup_pending);
    for (const auto* replacement : {"true", "0", "null"}) {
      auto malformed = encoded;
      const auto offset = malformed.find("\"cleanup_pending\":false");
      check(offset != std::string::npos);
      malformed.replace(offset + std::string_view{"\"cleanup_pending\":"}.size(), 5U, replacement);
      envelope.payload.assign(malformed.begin(), malformed.end());
      check(decode_ipc_scene_progress(envelope, decoded) == result::invalid_argument);
    }
    for (const auto* replacement : {"0", "null", "\"true\""}) {
      auto malformed = encoded;
      malformed.replace(cleanup_offset + std::string_view{",\"cleanup_complete\":"}.size(), 4U,
                        replacement);
      envelope.payload.assign(malformed.begin(), malformed.end());
      check(decode_ipc_scene_progress(envelope, decoded) == result::invalid_argument);
      check(decoded.budget && !decoded.budget->cleanup_complete);
    }
    for (const auto* replacement : {"-1", "1.5", "null", "18446744073709551616"}) {
      auto malformed = encoded;
      const auto begin = malformed.find("18446744073709551615");
      check(begin != std::string::npos);
      malformed.replace(begin, 20U, replacement);
      envelope.payload.assign(malformed.begin(), malformed.end());
      check(decode_ipc_scene_progress(envelope, decoded) == result::invalid_argument);
      check(decoded.budget && decoded.budget->candidate_logical_bytes == UINT64_MAX);
    }
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
