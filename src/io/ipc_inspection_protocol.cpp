// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_inspection_protocol.h"

#include <yyjson.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <utility>

namespace {

constexpr std::size_t max_changes = 8192U;
constexpr std::size_t max_string_size = 16U * 1024U;

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

bool add_id(yyjson_mut_doc* document, yyjson_mut_val* object, const char* key,
            gneiss::ipc_runtime_object_id id) noexcept {
  auto* value = yyjson_mut_obj(document);
  return value != nullptr && yyjson_mut_obj_add_uint(document, value, "value", id.value) &&
         yyjson_mut_obj_add_uint(document, value, "generation", id.generation) &&
         yyjson_mut_obj_add_val(document, object, key, value);
}

bool parse_id(yyjson_val* object, const char* key, bool allow_invalid,
              gneiss::ipc_runtime_object_id& output) noexcept {
  auto* value = yyjson_obj_get(object, key);
  auto* id = yyjson_is_obj(value) ? yyjson_obj_get(value, "value") : nullptr;
  auto* generation = yyjson_is_obj(value) ? yyjson_obj_get(value, "generation") : nullptr;
  if (!yyjson_is_uint(id) || !yyjson_is_uint(generation) ||
      yyjson_get_uint(generation) > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  output = {yyjson_get_uint(id), static_cast<std::uint32_t>(yyjson_get_uint(generation))};
  return allow_invalid ? (output.is_valid() || (output.value == 0U && output.generation == 0U))
                       : output.is_valid();
}

bool add_float_array(yyjson_mut_doc* document, yyjson_mut_val* object, const char* key,
                     const float* values, std::size_t count) noexcept {
  auto* array = yyjson_mut_arr(document);
  if (array == nullptr) {
    return false;
  }
  for (std::size_t index = 0U; index < count; ++index) {
    if (!std::isfinite(values[index]) || !yyjson_mut_arr_add_real(document, array, values[index])) {
      return false;
    }
  }
  return yyjson_mut_obj_add_val(document, object, key, array);
}

bool parse_float_array(yyjson_val* object, const char* key, float* output,
                       std::size_t count) noexcept {
  auto* array = yyjson_obj_get(object, key);
  if (!yyjson_is_arr(array) || yyjson_arr_size(array) != count) {
    return false;
  }
  for (std::size_t index = 0U; index < count; ++index) {
    auto* value = yyjson_arr_get(array, index);
    if (!yyjson_is_num(value)) {
      return false;
    }
    const auto number = yyjson_get_real(value);
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max()) {
      return false;
    }
    output[index] = static_cast<float>(number);
  }
  return true;
}

bool add_node(yyjson_mut_doc* document, yyjson_mut_val* change,
              const gneiss::ipc_inspection_node& node) noexcept {
  auto* value = yyjson_mut_obj(document);
  return value != nullptr && add_id(document, value, "id", node.id) &&
         add_id(document, value, "parent", node.parent) &&
         yyjson_mut_obj_add_strncpy(document, value, "uuid", node.uuid.data(), node.uuid.size()) &&
         yyjson_mut_obj_add_strncpy(document, value, "name", node.name.data(), node.name.size()) &&
         add_float_array(document, value, "translation", node.local_transform.translation, 3U) &&
         add_float_array(document, value, "rotation", node.local_transform.rotation, 4U) &&
         add_float_array(document, value, "scale", node.local_transform.scale, 3U) &&
         yyjson_mut_obj_add_uint(document, value, "component_flags", node.component_flags) &&
         yyjson_mut_obj_add_val(document, change, "node", value);
}

bool parse_node(yyjson_val* change, gneiss::ipc_inspection_node& output) {
  auto* node = yyjson_obj_get(change, "node");
  auto* uuid = yyjson_is_obj(node) ? yyjson_obj_get(node, "uuid") : nullptr;
  auto* name = yyjson_is_obj(node) ? yyjson_obj_get(node, "name") : nullptr;
  auto* flags = yyjson_is_obj(node) ? yyjson_obj_get(node, "component_flags") : nullptr;
  if (!yyjson_is_str(uuid) || yyjson_get_len(uuid) == 0U ||
      yyjson_get_len(uuid) > max_string_size || !yyjson_is_str(name) ||
      yyjson_get_len(name) > max_string_size || !yyjson_is_uint(flags) ||
      yyjson_get_uint(flags) > std::numeric_limits<std::uint32_t>::max() ||
      !parse_id(node, "id", false, output.id) || !parse_id(node, "parent", true, output.parent) ||
      !parse_float_array(node, "translation", output.local_transform.translation, 3U) ||
      !parse_float_array(node, "rotation", output.local_transform.rotation, 4U) ||
      !parse_float_array(node, "scale", output.local_transform.scale, 3U)) {
    return false;
  }
  output.uuid.assign(yyjson_get_str(uuid), yyjson_get_len(uuid));
  output.name.assign(yyjson_get_str(name), yyjson_get_len(name));
  output.component_flags = static_cast<std::uint32_t>(yyjson_get_uint(flags));
  return true;
}

} // namespace

namespace gneiss {

result encode_ipc_inspection_batch(const ipc_inspection_batch& batch, ipc_frame& output) noexcept {
  if (batch.stamp.session_id == 0U || batch.stamp.sequence == 0U ||
      batch.changes.size() > max_changes) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* changes = document ? yyjson_mut_arr(document.get()) : nullptr;
    if (root == nullptr || changes == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", batch.stamp.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "sequence", batch.stamp.sequence) ||
        !yyjson_mut_obj_add_bool(document.get(), root, "full", batch.is_full)) {
      return result::out_of_memory;
    }
    for (const auto& change : batch.changes) {
      auto* object = yyjson_mut_obj(document.get());
      const auto* operation =
          change.type == ipc_inspection_change_type::upsert ? "upsert" : "remove";
      if (object == nullptr || !change.id.is_valid() ||
          !yyjson_mut_obj_add_str(document.get(), object, "operation", operation) ||
          !add_id(document.get(), object, "id", change.id) ||
          (change.type == ipc_inspection_change_type::upsert &&
           (change.node.id != change.id || change.node.uuid.empty() ||
            change.node.uuid.size() > max_string_size ||
            change.node.name.size() > max_string_size ||
            !add_node(document.get(), object, change.node))) ||
          !yyjson_mut_arr_add_val(changes, object)) {
        return result::invalid_argument;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "changes", changes)) {
      return result::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t length = 0U;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_NOFLAG, &length), &std::free);
    if (!json) {
      return result::out_of_memory;
    }
    if (length > ipc_protocol_max_json_size) {
      return result::invalid_argument;
    }
    ipc_frame frame;
    frame.protocol_major = ipc_protocol_major;
    frame.protocol_minor = ipc_protocol_minor;
    frame.message_type = static_cast<std::uint16_t>(ipc_message_type::inspection_snapshot);
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

result decode_ipc_inspection_batch(const ipc_frame& frame, ipc_inspection_batch& output) noexcept {
  if (frame.protocol_major != ipc_protocol_major ||
      frame.message_type != static_cast<std::uint16_t>(ipc_message_type::inspection_snapshot) ||
      frame.payload.size() > ipc_protocol_max_json_size) {
    return result::unsupported;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(frame.payload.data()),
                                      frame.payload.size(), YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* session = yyjson_is_obj(root) ? yyjson_obj_get(root, "session_id") : nullptr;
    auto* sequence = yyjson_is_obj(root) ? yyjson_obj_get(root, "sequence") : nullptr;
    auto* full = yyjson_is_obj(root) ? yyjson_obj_get(root, "full") : nullptr;
    auto* changes = yyjson_is_obj(root) ? yyjson_obj_get(root, "changes") : nullptr;
    if (!yyjson_is_uint(session) || yyjson_get_uint(session) == 0U || !yyjson_is_uint(sequence) ||
        yyjson_get_uint(sequence) == 0U || !yyjson_is_bool(full) || !yyjson_is_arr(changes) ||
        yyjson_arr_size(changes) > max_changes) {
      return result::invalid_argument;
    }
    ipc_inspection_batch pending;
    pending.stamp = {yyjson_get_uint(session), yyjson_get_uint(sequence)};
    pending.is_full = yyjson_get_bool(full);
    std::set<std::pair<std::uint64_t, std::uint32_t>> seen;
    std::size_t index = 0U;
    std::size_t count = 0U;
    yyjson_val* value = nullptr;
    yyjson_arr_foreach(changes, index, count, value) {
      auto* operation = yyjson_is_obj(value) ? yyjson_obj_get(value, "operation") : nullptr;
      ipc_inspection_change change;
      if (!yyjson_is_str(operation) || !parse_id(value, "id", false, change.id) ||
          !seen.emplace(change.id.value, change.id.generation).second) {
        return result::invalid_argument;
      }
      const std::string_view name(yyjson_get_str(operation), yyjson_get_len(operation));
      if (name == "upsert") {
        change.type = ipc_inspection_change_type::upsert;
        if (!parse_node(value, change.node) || change.node.id != change.id) {
          return result::invalid_argument;
        }
      } else if (name == "remove") {
        change.type = ipc_inspection_change_type::remove;
      } else {
        return result::invalid_argument;
      }
      pending.changes.push_back(std::move(change));
    }
    output = std::move(pending);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss
