// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_inspection_protocol.h"
#include "ipc_log_protocol.h"
#include "ipc_property_protocol.h"
#include "ipc_statistics_protocol.h"

#include <array>
#include <string>

namespace {

[[nodiscard]] gneiss_type_id test_type_id() noexcept {
  gneiss_type_id id{};
  for (std::uint8_t index = 0U; index < 16U; ++index) {
    id.bytes[index] = static_cast<std::uint8_t>(index + 1U);
  }
  return id;
}

[[nodiscard]] bool test_log_and_statistics() {
  gneiss::ipc_envelope envelope;
  const std::string event = R"({"level":"INFO","message":"运行中"})";
  std::string decoded_event;
  if (gneiss::encode_ipc_log_event(event, envelope) != gneiss::result::success ||
      gneiss::decode_ipc_log_event(envelope, decoded_event) != gneiss::result::success ||
      decoded_event != event) {
    return false;
  }
  const gneiss::ipc_runtime_statistics statistics{.session_id = 2U,
                                                  .sequence = 3U,
                                                  .frame_index = 4U,
                                                  .frame_delta_ns = 5U,
                                                  .fixed_update_count = 6U,
                                                  .scene_node_count = 7U,
                                                  .entity_count = 8U,
                                                  .ipc_pending_writes = 9U,
                                                  .ipc_dropped_events = 10U};
  gneiss::ipc_runtime_statistics decoded;
  return gneiss::encode_ipc_statistics_v2(statistics, envelope) == gneiss::result::success &&
         gneiss::decode_ipc_statistics_v2(envelope, decoded) == gneiss::result::success &&
         decoded.sequence == statistics.sequence && decoded.entity_count == statistics.entity_count;
}

[[nodiscard]] bool test_inspection() {
  const gneiss::ipc_inspection_batch batch{
      .stamp = {.session_id = 3U, .sequence = 4U},
      .is_full = false,
      .chunk_index = 0U,
      .chunk_count = 1U,
      .changes = {{.type = gneiss::ipc_inspection_change_type::remove,
                   .id = {.value = 7U, .generation = 1U},
                   .node = {}}}};
  std::vector<gneiss::ipc_envelope> envelopes;
  if (gneiss::encode_ipc_inspection_batch_v2(batch, envelopes) != gneiss::result::success ||
      envelopes.size() != 1U) {
    return false;
  }
  gneiss::ipc_inspection_batch decoded;
  if (gneiss::decode_ipc_inspection_batch_v2(envelopes.front(), decoded) !=
          gneiss::result::success ||
      decoded.stamp.sequence != batch.stamp.sequence || decoded.changes.size() != 1U) {
    return false;
  }
  gneiss::ipc_envelope resync;
  return gneiss::encode_ipc_inspection_resync(9U, resync) == gneiss::result::success &&
         gneiss::decode_ipc_inspection_resync(resync) == gneiss::result::success &&
         resync.request_id == 9U && resync.kind == gneiss::ipc_message_kind::request;
}

[[nodiscard]] bool test_property_request_and_response() {
  const gneiss::ipc_property_write command{.session_id = 9U,
                                           .command_id = 21U,
                                           .object = {.value = 5U, .generation = 2U},
                                           .type_id = test_type_id(),
                                           .field_id = 7U,
                                           .expected_revision = 11U,
                                           .value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::ipc_envelope envelope;
  gneiss::ipc_property_write decoded_command;
  if (gneiss::encode_ipc_property_write_v2(command, 21U, envelope) != gneiss::result::success ||
      gneiss::decode_ipc_property_write_v2(envelope, decoded_command) != gneiss::result::success ||
      decoded_command.command_id != envelope.request_id) {
    return false;
  }
  auto mismatched = envelope;
  ++mismatched.request_id;
  if (gneiss::decode_ipc_property_write_v2(mismatched, decoded_command) !=
          gneiss::result::success ||
      decoded_command.command_id != mismatched.request_id) {
    return false;
  }
  const gneiss::ipc_property_write_result response{
      .session_id = 9U,
      .command_id = 21U,
      .code = 0,
      .revision = 12U,
      .message = "已应用",
      .canonical_value = {std::array<float, 3>{1.0F, 2.0F, 3.0F}}};
  gneiss::ipc_property_write_result decoded_response;
  return gneiss::encode_ipc_property_result_v2(response, 21U, envelope) ==
             gneiss::result::success &&
         gneiss::decode_ipc_property_result_v2(envelope, decoded_response) ==
             gneiss::result::success &&
         decoded_response.command_id == envelope.request_id &&
         decoded_response.revision == response.revision;
}

[[nodiscard]] bool test_rules_and_rejections() {
  gneiss::ipc_envelope envelope;
  gneiss::ipc_property_write command;
  command.session_id = 1U;
  command.command_id = 2U;
  return gneiss::encode_ipc_property_write_v2(command, 3U, envelope) ==
             gneiss::result::invalid_argument &&
         gneiss::encode_ipc_inspection_resync(0U, envelope) == gneiss::result::invalid_argument &&
         gneiss::ipc_log_operations().size() == 1U &&
         gneiss::ipc_inspection_operations().size() == 2U &&
         gneiss::ipc_statistics_operations().size() == 1U &&
         gneiss::ipc_property_operations().size() == 1U;
}

[[nodiscard]] bool test_inspection_sequence_tracker() {
  gneiss::ipc_inspection_sequence_tracker tracker;
  if (tracker.begin(7U, 2U) != gneiss::result::success ||
      tracker.observe({7U, 2U}) != gneiss::ipc_inspection_sequence_result::accepted ||
      tracker.observe({7U, 2U}) != gneiss::ipc_inspection_sequence_result::duplicate ||
      tracker.observe({7U, 4U}) != gneiss::ipc_inspection_sequence_result::gap ||
      tracker.observe({8U, 3U}) != gneiss::ipc_inspection_sequence_result::stale_session ||
      tracker.observe({7U, 3U}) != gneiss::ipc_inspection_sequence_result::accepted) {
    return false;
  }
  tracker.reset();
  return tracker.observe({7U, 4U}) == gneiss::ipc_inspection_sequence_result::invalid;
}

} // namespace

int main() {
  try {
    return test_log_and_statistics() && test_inspection() && test_property_request_and_response() &&
                   test_rules_and_rejections() && test_inspection_sequence_tracker()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
