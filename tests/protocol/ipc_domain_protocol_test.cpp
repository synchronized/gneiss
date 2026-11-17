// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/protocol/registration.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/resource/scene/game_object_description.hpp>
#include <gneiss/protocol/ipc_inspection_protocol.hpp>
#include <gneiss/protocol/ipc_log_protocol.hpp>
#include <gneiss/protocol/ipc_property_protocol.hpp>
#include <gneiss/protocol/ipc_statistics_protocol.hpp>

#include <gneiss/protocol/payload.hpp>
#include <array>
#include <string>

using namespace gneiss::host;

namespace {

[[nodiscard]] gneiss::guid test_type_id() noexcept {
  gneiss::guid::byte_array bytes{};
  for (std::uint8_t index = 0U; index < 16U; ++index) {
    bytes[index] = static_cast<std::uint8_t>(index + 1U);
  }
  return gneiss::guid::from_bytes(bytes);
}

[[nodiscard]] bool test_log_and_statistics() {
  gneiss::ipc::envelope envelope;
  runtime_log_record record{.severity = 3, .sequence = 4, .timestamp_ns = 5,
                            .thread_id = 6, .context = 7, .source = "runtime",
                            .category = "scene", .message = "运行中\n多行\t测试",
                            .operation = gneiss::error::invalid_argument};
  auto encoded_log = gneiss::protocol::encode(log_record, record);
  if (!encoded_log) return false;
  auto decoded_log = gneiss::protocol::decode(log_record, *encoded_log);
  if (!decoded_log || decoded_log->message != record.message || decoded_log->sequence != 4 ||
      decoded_log->timestamp_ns != 5 || decoded_log->thread_id != 6 || decoded_log->context != 7 ||
      decoded_log->severity != 3 || decoded_log->source != record.source ||
      decoded_log->category != record.category || decoded_log->operation != record.operation)
    return false;
  auto invalid = *encoded_log;
  invalid.request_id = 1;
  if (gneiss::protocol::decode(log_record, invalid)) return false;
  invalid = *encoded_log;
  invalid.payload = {'{', '}'};
  if (gneiss::protocol::decode(log_record, invalid)) return false;
  invalid.payload.resize(16385);
  if (gneiss::protocol::decode(log_record, invalid)) return false;
  record.version = 99;
  if (gneiss::protocol::encode(log_record, record)) return false;
  record.version = 2;
  record.message.assign(16385, 'x');
  if (gneiss::protocol::encode(log_record, record)) return false;
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
             gneiss::error::success &&
         gneiss::host::decode_ipc_statistics_v2(envelope, decoded) ==
             gneiss::error::success &&
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
  std::vector<gneiss::ipc::envelope> envelopes;
  if (gneiss::host::encode_ipc_inspection_batch_v2(batch, envelopes) !=
          gneiss::error::success ||
      envelopes.size() != 1U) {
    return false;
  }
  gneiss::host::ipc_inspection_batch decoded;
  if (gneiss::host::decode_ipc_inspection_batch_v2(envelopes.front(), decoded) !=
          gneiss::error::success ||
      decoded.stamp.sequence != batch.stamp.sequence || decoded.changes.size() != 1U) {
    return false;
  }
  auto large = batch;
  large.changes.clear();
  for (std::uint64_t i = 1; i <= 12; ++i) {
    auto& change = large.changes.emplace_back();
    change.id = change.node.id = {i, 1};
    change.node.name.assign(12000, 'x');
  }
  if (encode_ipc_inspection_batch_v2(large, envelopes).failed() || envelopes.size() < 2)
    return false;
  std::size_t count = 0;
  for (std::size_t i = 0; i < envelopes.size(); ++i) {
    if (envelopes[i].payload.size() > 65536 ||
        decode_ipc_inspection_batch_v2(envelopes[i], decoded).failed() ||
        decoded.chunk_index != i || decoded.chunk_count != envelopes.size()) return false;
    count += decoded.changes.size();
  }
  if (count != large.changes.size()) return false;
  auto duplicate = large;
  duplicate.changes[1].id = duplicate.changes[1].node.id = duplicate.changes[0].id;
  std::vector<std::uint8_t> rejected;
  if (encode_ipc_inspection_batch(duplicate, rejected).ok()) return false;
  gneiss::ipc::envelope resync;
  return gneiss::host::encode_ipc_inspection_resync(9U, resync) == gneiss::error::success &&
         gneiss::host::decode_ipc_inspection_resync(resync) == gneiss::error::success &&
         resync.request_id == 9U && resync.kind == gneiss::ipc::message_kind::request;
}

[[nodiscard]] bool test_inspection_guid_boundary() {
  using gneiss::guid;
  const auto id = *guid::parse("abcdef01-2345-4678-9abc-def012345678");
  ipc_inspection_batch batch{.stamp = {1U, 1U}, .is_full = true, .changes = {}};
  auto& change = batch.changes.emplace_back();
  change.id = change.node.id = {1U, 1U};
  change.node.uuid = id;
  const gneiss::camera_description camera{{1.0F, 0.25F, 500.0F}, true};
  change.node.components.set(camera);
  std::vector<std::uint8_t> bytes;
  ipc_inspection_batch decoded;
  if (encode_ipc_inspection_batch(batch, bytes).failed() ||
      decode_ipc_inspection_batch(bytes, decoded).failed() || decoded.changes.size() != 1U ||
      decoded.changes[0].node.uuid != id || decoded.changes[0].node.prefab_instance_uuid)
    return false;
  const auto* restored_camera = decoded.changes[0].node.components.get<gneiss::camera_description>();
  if (!restored_camera || restored_camera->uuid != camera.uuid ||
      restored_camera->settings.fov != camera.settings.fov ||
      restored_camera->settings.near_plane != camera.settings.near_plane ||
      restored_camera->settings.far_plane != camera.settings.far_plane ||
      !restored_camera->is_primary)
    return false;
  const std::string text(bytes.begin(), bytes.end());
  if (text.find(id.string()) == std::string::npos)
    return false;
  for (const auto malformed : {"invalid"}) {
    auto broken = text;
    broken.replace(broken.find(id.string()), 36U, malformed);
    std::vector<std::uint8_t> payload(broken.begin(), broken.end());
    if (decode_ipc_inspection_batch(payload, decoded).ok() || decoded.changes[0].node.uuid != id)
      return false;
  }
  batch.changes[0].node.prefab_instance_uuid = id;
  batch.changes[0].node.prefab_source_node_uuid = id;
  if (encode_ipc_inspection_batch(batch, bytes).failed() ||
      decode_ipc_inspection_batch(bytes, decoded).failed() ||
      decoded.changes[0].node.prefab_source_node_uuid != id)
    return false;
  batch.changes[0].node.prefab_instance_uuid = {};
  if (encode_ipc_inspection_batch(batch, bytes).ok())
    return false;
  batch.changes[0].node.uuid = {};
  return encode_ipc_inspection_batch(batch, bytes).failed();
}

[[nodiscard]] bool test_property_request_and_response() {
  const gneiss::host::ipc_property_write command{
      .session_id = 9U,
      .command_id = 21U,
      .object = {.value = 5U, .generation = 2U},
      .type_id = test_type_id(),
      .field_id = 7U,
      .expected_revision = 11U,
      .value = {gneiss::math::vector3{1.0F, 2.0F, 3.0F}}};
  auto envelope = gneiss::protocol::encode(property_write, command, 21U);
  if (!envelope) return false;
  auto decoded_command = gneiss::protocol::decode(property_write, *envelope);
  if (!decoded_command || decoded_command->command_id != 0 || envelope->request_id != 21U)
    return false;
  const gneiss::host::ipc_property_write_result response{
      .session_id = 9U,
      .command_id = 21U,
      .code = 0,
      .revision = 12U,
      .message = "已应用",
      .canonical_value = {gneiss::math::vector3{1.0F, 2.0F, 3.0F}}};
  auto result = gneiss::protocol::encode(property_result, response, 21U);
  if (!result) return false;
  auto decoded = gneiss::protocol::decode(property_result, *result);
  return decoded && decoded->revision == response.revision && result->request_id == 21U;
}

[[nodiscard]] bool test_reflected_statistics() {
  ipc_runtime_statistics original{1, 2, 3, 4, 5, 6, 7, 8};
  auto bytes = gneiss::protocol::encode_payload(original, 65536);
  if (!bytes || gneiss::protocol::encode_payload(original, 1)) return false;
  if (gneiss::protocol::decode_payload<ipc_runtime_statistics>(*bytes, bytes->size()-1)) return false;
  for (const std::string& text : {std::string("{}"), std::string("{"),
       std::string(R"({"session_id":-1})")}) {
    const auto input = std::span{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
    auto result = gneiss::protocol::decode_payload<ipc_runtime_statistics>(input, 65536);
    if (result || result.error().message.empty()) return false;
    auto output = original;
    if (decode_ipc_runtime_statistics(input, output).ok() || output.sequence != original.sequence) return false;
  }
  auto invalid = original;
  invalid.session_id = 0;
  auto encoded_invalid = gneiss::protocol::encode_payload(invalid, 65536);
  auto output = original;
  return encoded_invalid && decode_ipc_runtime_statistics(*encoded_invalid, output).failed() &&
         output.session_id == original.session_id && output.sequence == original.sequence;
}

[[nodiscard]] bool test_rules_and_rejections() {
  gneiss::ipc::envelope envelope;
  gneiss::host::ipc_property_write command;
  command.session_id = 1U;
  command.command_id = 2U;
  return !gneiss::protocol::encode(property_write, command, 3U) &&
         gneiss::host::encode_ipc_inspection_resync(0U, envelope) ==
             gneiss::error::invalid_argument &&
         gneiss::host::ipc_log_operations().size() == 1U &&
         gneiss::host::ipc_inspection_operations().size() == 2U &&
         gneiss::host::ipc_statistics_operations().size() == 1U &&
         gneiss::host::ipc_property_operations().size() == 1U;
}

} // namespace

int main() {
  try {
    if (gneiss::initialize_meta(gneiss::protocol::register_types).failed())
      return 2;
    return test_reflected_statistics() && test_log_and_statistics() && test_inspection() && test_inspection_guid_boundary() &&
                   test_property_request_and_response() && test_rules_and_rejections()
               ? 0
               : 1;
  } catch (...) {
    return 1;
  }
}
