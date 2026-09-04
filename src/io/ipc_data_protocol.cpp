// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_data_protocol.h"

#include <array>
#include <limits>
#include <new>
#include <utility>

namespace {

constexpr std::size_t max_log_event_size = std::size_t{16U} * 1024U;

constexpr auto event_kind = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::event);
constexpr auto request_kind = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::request);
constexpr auto response_kind = gneiss::ipc_kind_mask(gneiss::ipc_message_kind::response);

constexpr std::array log_operations{gneiss::ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(gneiss::ipc_log_operation::event),
    .editor_to_runtime_kinds = 0U,
    .runtime_to_editor_kinds = event_kind}};
constexpr std::array inspection_operations{
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_inspection_operation::snapshot),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = event_kind},
    gneiss::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(gneiss::ipc_inspection_operation::resync),
        .editor_to_runtime_kinds = request_kind,
        .runtime_to_editor_kinds = 0U}};
constexpr std::array statistics_operations{gneiss::ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(gneiss::ipc_statistics_operation::snapshot),
    .editor_to_runtime_kinds = 0U,
    .runtime_to_editor_kinds = event_kind}};
constexpr std::array property_operations{gneiss::ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(gneiss::ipc_property_operation::write),
    .editor_to_runtime_kinds = request_kind,
    .runtime_to_editor_kinds = response_kind}};

[[nodiscard]] gneiss::ipc_frame legacy_frame(const gneiss::ipc_envelope& envelope,
                                             gneiss::ipc_message_type type) {
  return {.protocol_major = gneiss::ipc_protocol_major,
          .protocol_minor = gneiss::ipc_protocol_minor,
          .message_type = static_cast<std::uint16_t>(type),
          .flags = 0U,
          .payload = envelope.payload};
}

[[nodiscard]] bool matches(const gneiss::ipc_envelope& envelope, gneiss::ipc_domain domain,
                           std::uint16_t operation, gneiss::ipc_message_kind kind) noexcept {
  return envelope.domain == domain && envelope.operation == operation && envelope.kind == kind;
}

} // namespace

namespace gneiss {

result encode_ipc_log_event(std::string_view event, ipc_envelope& output) noexcept {
  if (event.size() > max_log_event_size) {
    return result::invalid_argument;
  }
  try {
    ipc_envelope encoded{
        .domain = ipc_domain::log,
        .operation = static_cast<std::uint16_t>(ipc_log_operation::event),
        .kind = ipc_message_kind::event,
        .request_id = 0U,
        .payload = {reinterpret_cast<const std::uint8_t*>(event.data()),
                    reinterpret_cast<const std::uint8_t*>(event.data()) + event.size()}};
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_log_event(const ipc_envelope& envelope, std::string& output) noexcept {
  if (!matches(envelope, ipc_domain::log, static_cast<std::uint16_t>(ipc_log_operation::event),
               ipc_message_kind::event) ||
      envelope.request_id != 0U || envelope.payload.size() > max_log_event_size) {
    return result::invalid_argument;
  }
  try {
    std::string decoded(reinterpret_cast<const char*>(envelope.payload.data()),
                        envelope.payload.size());
    output = std::move(decoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result encode_ipc_inspection_batch_v2(const ipc_inspection_batch& batch,
                                      std::vector<ipc_envelope>& output) noexcept {
  std::vector<std::vector<std::uint8_t>> payloads;
  auto operation = encode_ipc_inspection_batch_chunks(batch, payloads);
  if (operation != result::success) {
    return operation;
  }
  try {
    std::vector<ipc_envelope> encoded;
    encoded.reserve(payloads.size());
    for (auto& payload : payloads) {
      encoded.push_back(
          {.domain = ipc_domain::inspection,
           .operation = static_cast<std::uint16_t>(ipc_inspection_operation::snapshot),
           .kind = ipc_message_kind::event,
           .request_id = 0U,
           .payload = std::move(payload)});
    }
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_inspection_batch_v2(const ipc_envelope& envelope,
                                      ipc_inspection_batch& output) noexcept {
  if (!matches(envelope, ipc_domain::inspection,
               static_cast<std::uint16_t>(ipc_inspection_operation::snapshot),
               ipc_message_kind::event) ||
      envelope.request_id != 0U) {
    return result::invalid_argument;
  }
  return decode_ipc_inspection_batch(envelope.payload, output);
}

result encode_ipc_inspection_resync(std::uint32_t request_id, ipc_envelope& output) noexcept {
  if (request_id == 0U) {
    return result::invalid_argument;
  }
  output = {.domain = ipc_domain::inspection,
            .operation = static_cast<std::uint16_t>(ipc_inspection_operation::resync),
            .kind = ipc_message_kind::request,
            .request_id = request_id,
            .payload = {}};
  return result::success;
}

result decode_ipc_inspection_resync(const ipc_envelope& envelope) noexcept {
  return matches(envelope, ipc_domain::inspection,
                 static_cast<std::uint16_t>(ipc_inspection_operation::resync),
                 ipc_message_kind::request) &&
                 envelope.request_id != 0U && envelope.payload.empty()
             ? result::success
             : result::invalid_argument;
}

result encode_ipc_statistics_v2(const ipc_runtime_statistics& statistics,
                                ipc_envelope& output) noexcept {
  std::vector<std::uint8_t> payload;
  const auto operation = encode_ipc_runtime_statistics(statistics, payload);
  if (operation != result::success) {
    return operation;
  }
  output = {.domain = ipc_domain::statistics,
            .operation = static_cast<std::uint16_t>(ipc_statistics_operation::snapshot),
            .kind = ipc_message_kind::event,
            .request_id = 0U,
            .payload = std::move(payload)};
  return result::success;
}

result decode_ipc_statistics_v2(const ipc_envelope& envelope,
                                ipc_runtime_statistics& output) noexcept {
  if (!matches(envelope, ipc_domain::statistics,
               static_cast<std::uint16_t>(ipc_statistics_operation::snapshot),
               ipc_message_kind::event) ||
      envelope.request_id != 0U) {
    return result::invalid_argument;
  }
  return decode_ipc_runtime_statistics(envelope.payload, output);
}

result encode_ipc_property_write_v2(const ipc_property_write& command, std::uint32_t request_id,
                                    ipc_envelope& output) noexcept {
  if (request_id == 0U || command.command_id != request_id) {
    return result::invalid_argument;
  }
  ipc_frame frame;
  const auto operation = encode_ipc_property_write(command, frame);
  if (operation != result::success) {
    return operation;
  }
  output = {.domain = ipc_domain::property,
            .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
            .kind = ipc_message_kind::request,
            .request_id = request_id,
            .payload = std::move(frame.payload)};
  return result::success;
}

result decode_ipc_property_write_v2(const ipc_envelope& envelope,
                                    ipc_property_write& output) noexcept {
  if (!matches(envelope, ipc_domain::property,
               static_cast<std::uint16_t>(ipc_property_operation::write),
               ipc_message_kind::request) ||
      envelope.request_id == 0U) {
    return result::invalid_argument;
  }
  try {
    ipc_property_write decoded;
    const auto operation = decode_ipc_property_write(
        legacy_frame(envelope, ipc_message_type::property_write), decoded);
    if (operation != result::success) {
      return operation;
    }
    if (decoded.command_id != envelope.request_id) {
      return result::invalid_argument;
    }
    output = std::move(decoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result encode_ipc_property_result_v2(const ipc_property_write_result& response,
                                     std::uint32_t request_id, ipc_envelope& output) noexcept {
  if (request_id == 0U || response.command_id != request_id) {
    return result::invalid_argument;
  }
  ipc_frame frame;
  const auto operation = encode_ipc_property_write_result(response, frame);
  if (operation != result::success) {
    return operation;
  }
  output = {.domain = ipc_domain::property,
            .operation = static_cast<std::uint16_t>(ipc_property_operation::write),
            .kind = ipc_message_kind::response,
            .request_id = request_id,
            .payload = std::move(frame.payload)};
  return result::success;
}

result decode_ipc_property_result_v2(const ipc_envelope& envelope,
                                     ipc_property_write_result& output) noexcept {
  if (!matches(envelope, ipc_domain::property,
               static_cast<std::uint16_t>(ipc_property_operation::write),
               ipc_message_kind::response) ||
      envelope.request_id == 0U) {
    return result::invalid_argument;
  }
  try {
    ipc_property_write_result decoded;
    const auto operation = decode_ipc_property_write_result(
        legacy_frame(envelope, ipc_message_type::property_write_result), decoded);
    if (operation != result::success) {
      return operation;
    }
    if (decoded.command_id != envelope.request_id) {
      return result::invalid_argument;
    }
    output = std::move(decoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

std::span<const ipc_operation_descriptor> ipc_log_operations() noexcept { return log_operations; }

std::span<const ipc_operation_descriptor> ipc_inspection_operations() noexcept {
  return inspection_operations;
}

std::span<const ipc_operation_descriptor> ipc_statistics_operations() noexcept {
  return statistics_operations;
}

std::span<const ipc_operation_descriptor> ipc_property_operations() noexcept {
  return property_operations;
}

} // namespace gneiss
