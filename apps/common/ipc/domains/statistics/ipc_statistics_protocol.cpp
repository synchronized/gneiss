// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_statistics_protocol.h"

#include <yyjson.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace gneiss {

namespace {

constexpr std::size_t max_json_size = 64U * 1024U;

} // namespace

result encode_ipc_runtime_statistics(const ipc_runtime_statistics& value,
                                     std::vector<std::uint8_t>& output) noexcept {
  if (value.session_id == 0U || value.sequence == 0U) {
    return result::invalid_argument;
  }
  try {
    std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> document(
        yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (root == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", value.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "sequence", value.sequence) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "frame_index", value.frame_index) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "frame_delta_ns", value.frame_delta_ns) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "fixed_update_count",
                                 value.fixed_update_count) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "scene_node_count",
                                 value.scene_node_count) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "entity_count", value.entity_count) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "ipc_pending_writes",
                                 value.ipc_pending_writes) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "ipc_dropped_events",
                                 value.ipc_dropped_events)) {
      return result::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t length = 0U;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_NOFLAG, &length), &std::free);
    if (!json || length > max_json_size) {
      return json ? result::invalid_argument : result::out_of_memory;
    }
    std::vector<std::uint8_t> encoded(reinterpret_cast<const std::uint8_t*>(json.get()),
                                      reinterpret_cast<const std::uint8_t*>(json.get()) + length);
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_runtime_statistics(std::span<const std::uint8_t> payload,
                                     ipc_runtime_statistics& output) noexcept {
  if (payload.size() > max_json_size) {
    return result::invalid_argument;
  }
  try {
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> document(
        yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                    YYJSON_READ_NOFLAG),
        &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    constexpr const char* names[] = {"session_id",     "sequence",           "frame_index",
                                     "frame_delta_ns", "fixed_update_count", "scene_node_count",
                                     "entity_count",   "ipc_pending_writes", "ipc_dropped_events"};
    std::uint64_t values[9]{};
    for (std::size_t index = 0U; index < std::size(names); ++index) {
      auto* field = yyjson_is_obj(root) ? yyjson_obj_get(root, names[index]) : nullptr;
      if (!yyjson_is_uint(field)) {
        return result::invalid_argument;
      }
      values[index] = yyjson_get_uint(field);
    }
    if (values[0] == 0U || values[1] == 0U) {
      return result::invalid_argument;
    }
    output = {values[0], values[1], values[2], values[3], values[4],
              values[5], values[6], values[7], values[8]};
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

namespace {

constexpr auto statistics_event_kind = ipc_kind_mask(ipc_message_kind::event);
constexpr std::array statistics_operations{ipc_operation_descriptor{
    .operation = static_cast<std::uint16_t>(ipc_statistics_operation::snapshot),
    .editor_to_runtime_kinds = 0U,
    .runtime_to_editor_kinds = statistics_event_kind}};

} // namespace

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
  if (envelope.domain != ipc_domain::statistics ||
      envelope.operation != static_cast<std::uint16_t>(ipc_statistics_operation::snapshot) ||
      envelope.kind != ipc_message_kind::event || envelope.request_id != 0U) {
    return result::invalid_argument;
  }
  return decode_ipc_runtime_statistics(envelope.payload, output);
}

std::span<const ipc_operation_descriptor> ipc_statistics_operations() noexcept {
  return statistics_operations;
}

} // namespace gneiss
