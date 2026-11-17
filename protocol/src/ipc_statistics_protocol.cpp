// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <array>
#include <gneiss/protocol/ipc_statistics_protocol.hpp>
#include <gneiss/protocol/payload.hpp>
#include <utility>
namespace gneiss::host {
namespace {
constexpr std::size_t max_json_size = 64U * 1024U;
}
error encode_ipc_runtime_statistics(const ipc_runtime_statistics& value,
                                    std::vector<std::uint8_t>& output) {
  if (value.session_id == 0 || value.sequence == 0)
    return error::invalid_argument;
  auto encoded = protocol::encode_payload(value, max_json_size);
  if (!encoded)
    return encoded.error().error;
  output = std::move(*encoded);
  return error::success;
}
error decode_ipc_runtime_statistics(std::span<const std::uint8_t> payload,
                                    ipc_runtime_statistics& output) {
  auto decoded = protocol::decode_payload<ipc_runtime_statistics>(payload, max_json_size);
  if (!decoded)
    return decoded.error().error;
  if (decoded->session_id == 0 || decoded->sequence == 0)
    return error::invalid_argument;
  output = std::move(*decoded);
  return error::success;
}
namespace {

constexpr auto statistics_event_kind =
    gneiss::protocol::kind_mask(gneiss::ipc::message_kind::event);
constexpr std::array statistics_operations{gneiss::protocol::operation_descriptor{
    .operation = static_cast<std::uint16_t>(ipc_statistics_operation::snapshot),
    .editor_to_runtime_kinds = 0U,
    .runtime_to_editor_kinds = statistics_event_kind}};

} // namespace

gneiss::error encode_ipc_statistics_v2(const ipc_runtime_statistics& statistics,
                                       gneiss::ipc::envelope& output) {
  std::vector<std::uint8_t> payload;
  const auto operation = encode_ipc_runtime_statistics(statistics, payload);
  if (operation != gneiss::error::success) {
    return operation;
  }
  output = {.domain = gneiss::protocol::domain::statistics,
            .operation = static_cast<std::uint16_t>(ipc_statistics_operation::snapshot),
            .kind = gneiss::ipc::message_kind::event,
            .request_id = 0U,
            .payload = std::move(payload)};
  return gneiss::error::success;
}

gneiss::error decode_ipc_statistics_v2(const gneiss::ipc::envelope& envelope,
                                       ipc_runtime_statistics& output) {
  if (envelope.domain != gneiss::protocol::domain::statistics ||
      envelope.operation != static_cast<std::uint16_t>(ipc_statistics_operation::snapshot) ||
      envelope.kind != gneiss::ipc::message_kind::event || envelope.request_id != 0U) {
    return gneiss::error::invalid_argument;
  }
  return decode_ipc_runtime_statistics(envelope.payload, output);
}

std::span<const gneiss::protocol::operation_descriptor> ipc_statistics_operations() noexcept {
  return statistics_operations;
}

} // namespace gneiss::host
