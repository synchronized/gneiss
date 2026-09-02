// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_statistics_protocol.h"

#include <yyjson.h>

#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace gneiss {

result encode_ipc_runtime_statistics(const ipc_runtime_statistics& value,
                                     ipc_frame& output) noexcept {
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
    if (!json || length > ipc_protocol_max_json_size) {
      return json ? result::invalid_argument : result::out_of_memory;
    }
    ipc_frame frame;
    frame.protocol_major = ipc_protocol_major;
    frame.protocol_minor = ipc_protocol_minor;
    frame.message_type = static_cast<std::uint16_t>(ipc_message_type::statistics_snapshot);
    frame.payload.assign(reinterpret_cast<const std::uint8_t*>(json.get()),
                         reinterpret_cast<const std::uint8_t*>(json.get()) + length);
    output = std::move(frame);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_runtime_statistics(const ipc_frame& frame,
                                     ipc_runtime_statistics& output) noexcept {
  if (frame.protocol_major != ipc_protocol_major ||
      frame.message_type != static_cast<std::uint16_t>(ipc_message_type::statistics_snapshot) ||
      frame.payload.size() > ipc_protocol_max_json_size) {
    return result::unsupported;
  }
  try {
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> document(
        yyjson_read(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size(),
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

} // namespace gneiss
