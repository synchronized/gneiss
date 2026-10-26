// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "ipc_inspection_protocol.hpp"
#include "ipc_log_protocol.hpp"
#include "ipc_property_protocol.hpp"
#include "ipc_statistics_protocol.hpp"

#include <array>
#include <string>

using namespace gneiss::host;

namespace {

[[nodiscard]] gneiss::engine::guid test_type_id() noexcept {
  gneiss::engine::guid::byte_array bytes{};
  for (std::uint8_t index = 0U; index < 16U; ++index) {
    bytes[index] = static_cast<std::uint8_t>(index + 1U);
  }
  return gneiss::engine::guid::from_bytes(bytes);
}

[[nodiscard]] bool test_log_and_statistics() {
  gneiss::engine::ipc_envelope envelope;
  const std::string event = R"({"level":"INFO","message":"运行中"})";
  std::string decoded_event;
  if (gneiss::host::encode_ipc_log_event(event, envelope) != gneiss::engine::result::success ||
      gneiss::host::decode_ipc_log_event(envelope, decoded_event) !=
          gneiss::engine::result::success ||
      decoded_event != event) {
    return false;
  }
  const gneiss::host::ipc_runtime_statistics statistics{.session_id = 2U,
                                                        .sequence = 3U,
                                                        .frame_index = 4U,
                                                        .frame_delta_ns = 5U,
                                                        .scene_node_count = 7U,
                                                        .entity_count = 8U,
                                                        .ipc_pending_writes = 9U,
                                                        .ipc_dropped_events = 10U};
  gneiss::host::ipc_runtime_statistics decoded;
  return gneiss::host::encode_ipc_statistics_v2(statistics, envelope) ==
             gneiss::engine::result::success &&
         gneiss::host::decode_ipc_statistics_v2(envelope, decoded) ==
             gneiss::engine::result::success &&
         decoded.session_id == statistics.session_id && decoded.sequence == statistics.sequence &&
         decoded.frame_index == statistics.frame_index &&
         decoded.frame_delta_ns == statistics.frame_delta_ns &&
         decoded.scene_node_count == statistics.scene_node_count &&
         decoded.entity_count == statistics.entity_count &&
         decoded.ipc_pending_writes == statistics.ipc_pending_writes &&
         decoded.ipc_dropped_events == statistics.ipc_dropped_events;
}

[[nodiscard]] bool test_inspection() {
  const gneiss::host::ipc_inspection_batch batch{
      .stamp = {.session_id = 3U, .sequence = 4U},
      .is_full = false,
      .chunk_index = 0U,
      .chunk_count = 1U,
      .changes = {{.type = gneiss::host::ipc_inspection_change_type::remove,
                   .id = {.value = 7U, .generation = 1U},
                   .node = {}}}};
  std::vector<gneiss::engine::ipc_envelope> envelopes;
  if (gneiss::host::encode_ipc_inspection_batch_v2(batch, envelopes) !=
          gneiss::engine::result::success ||
      envelopes.size() != 1U) {
    return false;
  }
  gneiss::host::ipc_inspection_batch decoded;
  if (gneiss::host::decode_ipc_inspection_batch_v2(envelopes.front(), decoded) !=
          gneiss::engine::result::success ||
      decoded.stamp.sequence != batch.stamp.sequence || decoded.changes.size() != 1U) {
    return false;
  }
  gneiss::engine::ipc_envelope resync;
  return gneiss::host::encode_ipc_inspection_resync(9U, resync) ==
             gneiss::engine::result::success &&
         gneiss::host::decode_ipc_inspection_resync(resync) == gneiss::engine::result::success &&
         resync.request_id == 9U && resync.kind == gneiss::engine::ipc_message_kind::request;
}

[[nodiscard]] bool test_property_request_and_response() {
  const gneiss::host::ipc_property_write command{.session_id = 9U,
                                                 .command_id = 21U,
                                                 .object = {.value = 5U, .generation = 2U},
                                                 .type_id = test_type_id(),
                                                 .field_id = 7U,
                                                 .expected_revision = 11U,
                                                 .value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::engine::ipc_envelope envelope;
  gneiss::host::ipc_property_write decoded_command;
  if (gneiss::host::encode_ipc_property_write_v2(command, 21U, envelope) !=
          gneiss::engine::result::success ||
      gneiss::host::decode_ipc_property_write_v2(envelope, decoded_command) !=
          gneiss::engine::result::success ||
      decoded_command.command_id != envelope.request_id) {
    return false;
  }
  auto mismatched = envelope;
  ++mismatched.request_id;
  if (gneiss::host::decode_ipc_property_write_v2(mismatched, decoded_command) !=
          gneiss::engine::result::success ||
      decoded_command.command_id != mismatched.request_id) {
    return false;
  }
  const gneiss::host::ipc_property_write_result response{
      .session_id = 9U,
      .command_id = 21U,
      .code = 0,
      .revision = 12U,
      .message = "已应用",
      .canonical_value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::host::ipc_property_write_result decoded_response;
  return gneiss::host::encode_ipc_property_result_v2(response, 21U, envelope) ==
             gneiss::engine::result::success &&
         gneiss::host::decode_ipc_property_result_v2(envelope, decoded_response) ==
             gneiss::engine::result::success &&
         decoded_response.command_id == envelope.request_id &&
         decoded_response.revision == response.revision;
}

[[nodiscard]] bool test_rules_and_rejections() {
  gneiss::engine::ipc_envelope envelope;
  gneiss::host::ipc_property_write command;
  command.session_id = 1U;
  command.command_id = 2U;
  return gneiss::host::encode_ipc_property_write_v2(command, 3U, envelope) ==
             gneiss::engine::result::invalid_argument &&
         gneiss::host::encode_ipc_inspection_resync(0U, envelope) ==
             gneiss::engine::result::invalid_argument &&
         gneiss::host::ipc_log_operations().size() == 1U &&
         gneiss::host::ipc_inspection_operations().size() == 2U &&
         gneiss::host::ipc_statistics_operations().size() == 1U &&
         gneiss::host::ipc_property_operations().size() == 1U;
}

} // namespace

int main() {
  try {
    return test_log_and_statistics() && test_inspection() && test_property_request_and_response() &&
                   test_rules_and_rejections()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
