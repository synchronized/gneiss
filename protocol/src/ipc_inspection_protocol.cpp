// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <array>
#include <gneiss/protocol/ipc_inspection_protocol.hpp>
#include <gneiss/protocol/payload.hpp>
#include <limits>
#include <set>

namespace gneiss::host {
namespace {
constexpr std::size_t max_changes = 8192U;
constexpr std::size_t max_string_size = 16U * 1024U;
constexpr std::uint32_t max_chunks = 4096U;
constexpr std::uint32_t max_outgoing_chunks = 128U;
constexpr std::size_t max_json_size = 64U * 1024U;

bool valid_batch(const ipc_inspection_batch& batch) {
  if (batch.stamp.session_id == 0 || batch.stamp.sequence == 0 ||
      batch.changes.size() > max_changes || batch.chunk_count == 0 ||
      batch.chunk_count > max_chunks || batch.chunk_index >= batch.chunk_count)
    return false;
  std::set<std::pair<std::uint64_t, std::uint32_t>> seen;
  for (const auto& change : batch.changes) {
    if (!change.id.is_valid() || !seen.emplace(change.id.value, change.id.generation).second)
      return false;
    if (change.type == ipc_inspection_change_type::remove)
      continue;
    const auto& node = change.node;
    if (change.type != ipc_inspection_change_type::upsert || node.id != change.id ||
        (!node.parent.is_valid() && (node.parent.value != 0 || node.parent.generation != 0)) ||
        (node.prefab_source_node_uuid && !node.prefab_instance_uuid) ||
        node.name.size() > max_string_size)
      return false;
  }
  return true;
}
}
const protocol::message_descriptor<ipc_inspection_batch> inspection_snapshot{
    protocol::domain::inspection,
    static_cast<std::uint16_t>(ipc_inspection_operation::snapshot),
    ipc::message_kind::event,
    protocol::peer_role::runtime,
    max_json_size,
    valid_batch};

error encode_ipc_inspection_batch(const ipc_inspection_batch& batch,
                                  std::vector<std::uint8_t>& output) {
  if (!valid_batch(batch))
    return error::invalid_argument;
  auto encoded = protocol::encode_payload(batch, max_json_size);
  if (!encoded)
    return encoded.error().error;
  output = std::move(*encoded);
  return error::success;
}

result<std::vector<ipc::envelope>, serialization::diagnostic>
encode_inspection_snapshot(const ipc_inspection_batch& batch) {
  if (!valid_batch(batch) || batch.chunk_index != 0 || batch.chunk_count != 1)
    return serialization::diagnostic{.message = "检查批次必须是合法的完整逻辑批次",
                                     .error = error::invalid_argument};
  std::vector<ipc_inspection_batch> chunks(1);
  chunks.front().stamp = batch.stamp;
  chunks.front().is_full = batch.is_full;
  for (const auto& change : batch.changes) {
    auto& current = chunks.back();
    current.changes.push_back(change);
    // 使用最大的序号与片数探测，避免最终帧头字段增长使负载超限。
    current.chunk_index = max_outgoing_chunks - 1;
    current.chunk_count = max_outgoing_chunks;
    auto probe = protocol::encode_payload(current, std::numeric_limits<std::size_t>::max());
    if (!probe)
      return std::move(probe).error();
    if (probe->size() <= max_json_size)
      continue;
    current.changes.pop_back();
    if (current.changes.empty())
      return serialization::diagnostic{.message = "单个检查对象超过分片负载上限",
                                       .error = error::invalid_argument};
    if (chunks.size() >= max_outgoing_chunks)
      return serialization::diagnostic{.message = "检查批次超过发送分片数量上限",
                                       .error = error::invalid_argument};
    chunks.push_back({.stamp = batch.stamp,
                      .is_full = batch.is_full,
                      .chunk_index = max_outgoing_chunks - 1,
                      .chunk_count = max_outgoing_chunks,
                      .changes = {change}});
    auto single = protocol::encode_payload(chunks.back(), max_json_size);
    if (!single)
      return std::move(single).error();
  }
  std::vector<ipc::envelope> frames;
  frames.reserve(chunks.size());
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    chunks[i].chunk_index = static_cast<std::uint32_t>(i);
    chunks[i].chunk_count = static_cast<std::uint32_t>(chunks.size());
    auto encoded = protocol::encode(inspection_snapshot, chunks[i]);
    if (!encoded)
      return std::move(encoded).error();
    frames.push_back(std::move(*encoded));
  }
  return frames;
}

error decode_ipc_inspection_batch(std::span<const std::uint8_t> payload,
                                  ipc_inspection_batch& output) {
  auto decoded = protocol::decode_payload<ipc_inspection_batch>(payload, max_json_size);
  if (!decoded)
    return decoded.error().error;
  if (!valid_batch(*decoded))
    return error::invalid_argument;
  output = std::move(*decoded);
  return error::success;
}

namespace {

constexpr auto inspection_request_kind =
    gneiss::protocol::kind_mask(gneiss::ipc::message_kind::request);
const std::array inspection_operations{
    inspection_snapshot.rule(),
    gneiss::protocol::operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_inspection_operation::resync),
        .editor_to_runtime_kinds = inspection_request_kind,
        .runtime_to_editor_kinds = 0U}};

[[nodiscard]] bool matches_inspection(const gneiss::ipc::envelope& envelope,
                                      ipc_inspection_operation operation,
                                      gneiss::ipc::message_kind kind) noexcept {
  return envelope.domain == gneiss::protocol::domain::inspection &&
         envelope.operation == static_cast<std::uint16_t>(operation) && envelope.kind == kind;
}

} // namespace

gneiss::error encode_ipc_inspection_resync(std::uint32_t request_id,
                                           gneiss::ipc::envelope& output) noexcept {
  if (request_id == 0U) {
    return gneiss::error::invalid_argument;
  }
  output = {.domain = gneiss::protocol::domain::inspection,
            .operation = static_cast<std::uint16_t>(ipc_inspection_operation::resync),
            .kind = gneiss::ipc::message_kind::request,
            .request_id = request_id,
            .payload = {}};
  return gneiss::error::success;
}

gneiss::error decode_ipc_inspection_resync(const gneiss::ipc::envelope& envelope) noexcept {
  return matches_inspection(envelope, ipc_inspection_operation::resync,
                            gneiss::ipc::message_kind::request) &&
                 envelope.request_id != 0U && envelope.payload.empty()
             ? gneiss::error::success
             : gneiss::error::invalid_argument;
}

std::span<const gneiss::protocol::operation_descriptor> ipc_inspection_operations() noexcept {
  return inspection_operations;
}

} // namespace gneiss::host
