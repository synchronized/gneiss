// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_inspection_protocol.h"
#include "ipc_protocol.h"
#include "ipc_statistics_protocol.h"
#include "ipc_transport.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

bool round_trip(const gneiss::ipc_message& source, gneiss::ipc_message& decoded) {
  gneiss::ipc_frame frame;
  return gneiss::encode_ipc_message(source, frame) == gneiss::result::success &&
         frame.protocol_major == gneiss::ipc_protocol_major &&
         frame.protocol_minor == gneiss::ipc_protocol_minor &&
         frame.message_type == static_cast<std::uint16_t>(source.type) &&
         gneiss::decode_ipc_message(frame, decoded) == gneiss::result::success &&
         decoded.type == source.type;
}

bool test_all_message_types() {
  gneiss::ipc_message source;
  gneiss::ipc_message decoded;

  source.type = gneiss::ipc_message_type::hello;
  source.session_token = "session-token";
  source.capabilities = {"control", "logs"};
  if (!round_trip(source, decoded) || decoded.session_token != source.session_token ||
      decoded.capabilities != source.capabilities) {
    return false;
  }

  source = {};
  source.type = gneiss::ipc_message_type::hello_ack;
  source.capabilities = {"control"};
  if (!round_trip(source, decoded) || decoded.capabilities != source.capabilities) {
    return false;
  }

  for (const auto type : {gneiss::ipc_message_type::ready, gneiss::ipc_message_type::pause,
                          gneiss::ipc_message_type::resume, gneiss::ipc_message_type::stop,
                          gneiss::ipc_message_type::inspection_resync}) {
    source = {};
    source.type = type;
    if (!round_trip(source, decoded)) {
      return false;
    }
  }

  for (const auto state : {gneiss::ipc_runtime_state::loading, gneiss::ipc_runtime_state::ready,
                           gneiss::ipc_runtime_state::running, gneiss::ipc_runtime_state::paused,
                           gneiss::ipc_runtime_state::stopping}) {
    source = {};
    source.type = gneiss::ipc_message_type::state_changed;
    source.runtime_state = state;
    if (!round_trip(source, decoded) || decoded.runtime_state != state) {
      return false;
    }
  }

  source = {};
  source.type = gneiss::ipc_message_type::log_event;
  source.text = R"({"level":"INFO","message":"已启动"})";
  if (!round_trip(source, decoded) || decoded.text != source.text) {
    return false;
  }

  source = {};
  source.type = gneiss::ipc_message_type::error;
  source.code = -9;
  source.text = "operation temporarily not ready";
  if (!round_trip(source, decoded) || decoded.code != source.code || decoded.text != source.text) {
    return false;
  }

  for (const auto type : {gneiss::ipc_message_type::ping, gneiss::ipc_message_type::pong}) {
    source = {};
    source.type = type;
    source.nonce = UINT64_C(0xFEDCBA9876543210);
    if (!round_trip(source, decoded) || decoded.nonce != source.nonce) {
      return false;
    }
  }

  source = {};
  source.type = gneiss::ipc_message_type::shutdown_complete;
  source.code = 0;
  return round_trip(source, decoded) && decoded.code == 0;
}

bool test_handshake_and_negotiation() {
  const std::vector<std::string> requested{
      "control", "logs", std::string(gneiss::ipc_capability_runtime_inspection_v2), "unknown",
      "control"};
  const std::vector<std::string> supported{
      "logs", "control", "diagnostics", std::string(gneiss::ipc_capability_runtime_inspection_v2)};
  gneiss::ipc_frame hello;
  if (gneiss::make_ipc_hello("secret", requested, hello) != gneiss::result::success) {
    return false;
  }
  hello.protocol_minor = 7U;
  gneiss::ipc_frame acknowledgment;
  std::vector<std::string> server_negotiated;
  if (gneiss::accept_ipc_hello(hello, "secret", supported, acknowledgment, server_negotiated) !=
          gneiss::result::success ||
      server_negotiated !=
          std::vector<std::string>(
              {"control", "logs", std::string(gneiss::ipc_capability_runtime_inspection_v2)}) ||
      acknowledgment.protocol_minor != gneiss::ipc_protocol_minor) {
    return false;
  }
  std::vector<std::string> client_negotiated;
  if (gneiss::accept_ipc_hello_ack(acknowledgment, requested, client_negotiated) !=
          gneiss::result::success ||
      client_negotiated != server_negotiated) {
    return false;
  }

  gneiss::ipc_frame unused;
  std::vector<std::string> unchanged{"unchanged"};
  if (gneiss::accept_ipc_hello(hello, "wrong", supported, unused, unchanged) !=
          gneiss::result::invalid_argument ||
      unchanged != std::vector<std::string>{"unchanged"}) {
    return false;
  }
  hello.protocol_major += 1U;
  return gneiss::accept_ipc_hello(hello, "secret", supported, unused, unchanged) ==
         gneiss::result::unsupported;
}

bool test_invalid_and_unknown_messages() {
  gneiss::ipc_message decoded;
  gneiss::ipc_frame frame;
  frame.protocol_major = gneiss::ipc_protocol_major;
  frame.protocol_minor = gneiss::ipc_protocol_minor;
  frame.message_type = 999U;
  frame.payload = {'{', '}'};
  if (gneiss::decode_ipc_message(frame, decoded) != gneiss::result::unsupported) {
    return false;
  }
  frame.message_type = static_cast<std::uint16_t>(gneiss::ipc_message_type::ping);
  frame.payload = {'{', '"', 'n', 'o', 'n', 'c', 'e', '"', ':', '"', 'x', '"', '}'};
  if (gneiss::decode_ipc_message(frame, decoded) != gneiss::result::invalid_argument) {
    return false;
  }
  frame.payload = {'n', 'u', 'l', 'l'};
  if (gneiss::decode_ipc_message(frame, decoded) != gneiss::result::invalid_argument) {
    return false;
  }

  gneiss::ipc_message oversized;
  oversized.type = gneiss::ipc_message_type::log_event;
  oversized.text.assign(17U * 1024U, 'x');
  return gneiss::encode_ipc_message(oversized, frame) == gneiss::result::invalid_argument;
}

bool test_handshake_and_heartbeat_timeouts() {
  using namespace std::chrono_literals;
  const auto start = gneiss::ipc_timeout_tracker::clock::time_point{};
  gneiss::ipc_timeout_tracker handshake(3s);
  handshake.reset(start);
  if (handshake.expired(start + 2999ms) || !handshake.expired(start + 3s)) {
    return false;
  }

  gneiss::ipc_timeout_tracker heartbeat(5s);
  heartbeat.reset(start);
  heartbeat.reset(start + 4s);
  if (heartbeat.expired(start + 8s) || !heartbeat.expired(start + 9s)) {
    return false;
  }
  gneiss::ipc_timeout_tracker invalid(0s);
  invalid.reset(start);
  return invalid.expired(start);
}

bool test_runtime_inspection_identity_and_sequence() {
  const gneiss::ipc_runtime_object_id invalid_object{};
  const gneiss::ipc_runtime_object_id object{42U, 1U};
  const gneiss::ipc_runtime_object_id recreated{42U, 2U};
  if (invalid_object.is_valid() || !object.is_valid() || object == recreated) {
    return false;
  }

  gneiss::ipc_inspection_sequence_tracker tracker;
  if (tracker.begin(0U) != gneiss::result::invalid_argument ||
      tracker.observe({1U, 1U}) != gneiss::ipc_inspection_sequence_result::invalid ||
      tracker.begin(100U) != gneiss::result::success || tracker.session_id() != 100U ||
      tracker.next_sequence() != 1U) {
    return false;
  }
  if (tracker.observe({100U, 1U}) != gneiss::ipc_inspection_sequence_result::accepted ||
      tracker.next_sequence() != 2U ||
      tracker.observe({100U, 1U}) != gneiss::ipc_inspection_sequence_result::duplicate ||
      tracker.observe({100U, 3U}) != gneiss::ipc_inspection_sequence_result::gap ||
      tracker.next_sequence() != 2U ||
      tracker.observe({99U, 2U}) != gneiss::ipc_inspection_sequence_result::stale_session ||
      tracker.observe({100U, 2U}) != gneiss::ipc_inspection_sequence_result::accepted) {
    return false;
  }
  tracker.reset();
  return tracker.session_id() == 0U && tracker.next_sequence() == 0U &&
         tracker.observe({100U, 3U}) == gneiss::ipc_inspection_sequence_result::invalid;
}

bool test_runtime_inspection_batch_round_trip() {
  gneiss::ipc_inspection_change root;
  root.id = {1U, 1U};
  root.node.id = root.id;
  root.node.uuid = "root";
  root.node.prefab_instance_uuid = "instance";
  root.node.prefab_source_node_uuid = "source";
  root.node.name = "根节点";
  root.node.local_transform.translation[0] = 2.0F;
  root.node.component_flags = GNEISS_SCENE_NODE_COMPONENT_CAMERA;
  root.node.camera.near_plane = 0.25F;
  root.node.mesh_uri = "asset:///meshes/root.gneiss-mesh";
  root.node.material_uri = "asset:///materials/root.material.json";
  gneiss::ipc_inspection_change removed;
  removed.type = gneiss::ipc_inspection_change_type::remove;
  removed.id = {2U, 3U};
  gneiss::ipc_inspection_batch source{.stamp = {8U, 4U},
                                      .is_full = false,
                                      .chunk_index = 1U,
                                      .chunk_count = 3U,
                                      .changes = {root, removed}};
  std::vector<std::uint8_t> payload;
  gneiss::ipc_inspection_batch decoded;
  return gneiss::encode_ipc_inspection_batch(source, payload) == gneiss::result::success &&
         gneiss::decode_ipc_inspection_batch(payload, decoded) == gneiss::result::success &&
         decoded.stamp.session_id == 8U && decoded.stamp.sequence == 4U && !decoded.is_full &&
         decoded.chunk_index == 1U && decoded.chunk_count == 3U && decoded.changes.size() == 2U &&
         decoded.changes[0].node.name == "根节点" &&
         decoded.changes[0].node.prefab_instance_uuid == "instance" &&
         decoded.changes[0].node.prefab_source_node_uuid == "source" &&
         decoded.changes[0].node.local_transform.translation[0] == 2.0F &&
         decoded.changes[0].node.component_flags == GNEISS_SCENE_NODE_COMPONENT_CAMERA &&
         decoded.changes[0].node.camera.near_plane == 0.25F &&
         decoded.changes[0].node.mesh_uri == root.node.mesh_uri &&
         decoded.changes[0].node.material_uri == root.node.material_uri &&
         decoded.changes[1].type == gneiss::ipc_inspection_change_type::remove &&
         decoded.changes[1].id == removed.id;
}

bool test_runtime_statistics_round_trip() {
  const gneiss::ipc_runtime_statistics source{7U, 3U, 120U, 16666667U, 240U, 12U, 15U, 2U, 1U};
  std::vector<std::uint8_t> payload;
  gneiss::ipc_runtime_statistics decoded;
  return gneiss::encode_ipc_runtime_statistics(source, payload) == gneiss::result::success &&
         gneiss::decode_ipc_runtime_statistics(payload, decoded) == gneiss::result::success &&
         decoded.session_id == source.session_id && decoded.sequence == source.sequence &&
         decoded.frame_index == source.frame_index &&
         decoded.frame_delta_ns == source.frame_delta_ns &&
         decoded.fixed_update_count == source.fixed_update_count &&
         decoded.scene_node_count == source.scene_node_count &&
         decoded.entity_count == source.entity_count &&
         decoded.ipc_pending_writes == source.ipc_pending_writes &&
         decoded.ipc_dropped_events == source.ipc_dropped_events;
}

bool test_runtime_inspection_chunking() {
  gneiss::ipc_inspection_batch source;
  source.stamp = {11U, 9U};
  source.is_full = true;
  for (std::uint64_t index = 1U; index <= 12U; ++index) {
    gneiss::ipc_inspection_change change;
    change.id = {index, 1U};
    change.node.id = change.id;
    change.node.uuid = "node-" + std::to_string(index);
    change.node.name.assign(8000U, 'n');
    source.changes.push_back(std::move(change));
  }
  std::vector<std::vector<std::uint8_t>> payloads;
  if (gneiss::encode_ipc_inspection_batch_chunks(source, payloads) != gneiss::result::success ||
      payloads.size() < 2U) {
    return false;
  }
  std::size_t change_count = 0U;
  for (std::size_t index = 0U; index < payloads.size(); ++index) {
    gneiss::ipc_inspection_batch decoded;
    if (gneiss::decode_ipc_inspection_batch(payloads[index], decoded) != gneiss::result::success ||
        decoded.stamp.session_id != 11U || decoded.stamp.sequence != 9U || !decoded.is_full ||
        decoded.chunk_index != index || decoded.chunk_count != payloads.size()) {
      return false;
    }
    change_count += decoded.changes.size();
  }
  return change_count == source.changes.size();
}

} // namespace

int main() {
  return test_all_message_types() && test_handshake_and_negotiation() &&
                 test_invalid_and_unknown_messages() && test_handshake_and_heartbeat_timeouts() &&
                 test_runtime_inspection_identity_and_sequence() &&
                 test_runtime_inspection_batch_round_trip() &&
                 test_runtime_statistics_round_trip() && test_runtime_inspection_chunking()
             ? 0
             : 1;
}
