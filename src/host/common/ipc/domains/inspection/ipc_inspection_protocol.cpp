// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_inspection_protocol.hpp"
#include "engine/core/error.hpp"

#include <yyjson.h>

#include <array>
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
constexpr std::uint32_t max_chunks = 4096U;
constexpr std::uint32_t max_outgoing_chunks = 128U;
constexpr std::size_t max_json_size = 64U * 1024U;

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

bool add_id(yyjson_mut_doc* document, yyjson_mut_val* object, const char* key,
            gneiss::host::ipc_runtime_object_id id) noexcept {
  auto* value = yyjson_mut_obj(document);
  return value != nullptr && yyjson_mut_obj_add_uint(document, value, "value", id.value) &&
         yyjson_mut_obj_add_uint(document, value, "generation", id.generation) &&
         yyjson_mut_obj_add_val(document, object, key, value);
}

bool parse_id(yyjson_val* object, const char* key, bool allow_invalid,
              gneiss::host::ipc_runtime_object_id& output) noexcept {
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

bool add_uuid(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* name,
              gneiss::engine::guid id) {
  const auto text = id ? id.string() : std::string{};
  return yyjson_mut_obj_add_strncpy(doc, object, name, text.data(), text.size());
}

bool parse_uuid(yyjson_val* value, bool optional, gneiss::engine::guid& output) {
  if (!yyjson_is_str(value))
    return false;
  const std::string_view text{yyjson_get_str(value), yyjson_get_len(value)};
  if (optional && text.empty()) {
    output = {};
    return true;
  }
  const auto parsed = gneiss::engine::guid::parse(text);
  if (!parsed || !*parsed || parsed->string() != text)
    return false;
  output = *parsed;
  return true;
}

// IPC 仅包装组件身份，字段编解码由已有反射序列化负责。
bool add_components(yyjson_mut_doc* doc, yyjson_mut_val* node,
                    const gneiss::engine::component_descriptions& components) {
  auto* array = yyjson_mut_arr(doc);
  if (!array)
    return false;
  for (const auto& component : components) {
    auto bytes = component.encode();
    if (!bytes)
      return false;
    document_ptr parsed(yyjson_read(reinterpret_cast<const char*>(bytes->data()), bytes->size(), 0),
                        &yyjson_doc_free);
    if (!parsed || !yyjson_is_obj(yyjson_doc_get_root(parsed.get())))
      return false;
    auto* entry = yyjson_mut_obj(doc);
    auto* content = yyjson_val_mut_copy(doc, yyjson_doc_get_root(parsed.get()));
    if (!entry || !content || !add_uuid(doc, entry, "$type_guid", component.guid()) ||
        !yyjson_mut_obj_add_val(doc, entry, "$content", content) ||
        !yyjson_mut_arr_add_val(array, entry))
      return false;
  }
  return yyjson_mut_obj_add_val(doc, node, "components", array);
}
bool parse_components(yyjson_val* node, gneiss::engine::component_descriptions& output) {
  auto* array = yyjson_obj_get(node, "components");
  if (!yyjson_is_arr(array))
    return false;
  gneiss::engine::component_descriptions result;
  for (std::size_t i = 0; i < yyjson_arr_size(array); ++i) {
    auto* entry = yyjson_arr_get(array, i);
    if (!yyjson_is_obj(entry))
      return false;
    gneiss::engine::guid type;
    auto* content = yyjson_obj_get(entry, "$content");
    if (!parse_uuid(yyjson_obj_get(entry, "$type_guid"), false, type) || !yyjson_is_obj(content) ||
        result.find(type))
      return false;
    std::size_t length{};
    std::unique_ptr<char, decltype(&std::free)> text(yyjson_val_write(content, 0, &length),
                                                     &std::free);
    if (!text)
      return false;
    auto decoded = gneiss::engine::component_description::decode(
        type, std::as_bytes(std::span{text.get(), length}));
    if (!decoded)
      return false;
    result.set(std::move(*decoded));
  }
  output = std::move(result);
  return true;
}

bool add_node(yyjson_mut_doc* document, yyjson_mut_val* change,
              const gneiss::host::ipc_inspection_node& node) {
  auto* value = yyjson_mut_obj(document);
  return value != nullptr && add_id(document, value, "id", node.id) &&
         add_id(document, value, "parent", node.parent) &&
         add_uuid(document, value, "uuid", node.uuid) &&
         add_uuid(document, value, "prefab_instance_uuid", node.prefab_instance_uuid) &&
         add_uuid(document, value, "prefab_source_node_uuid", node.prefab_source_node_uuid) &&
         yyjson_mut_obj_add_strncpy(document, value, "name", node.name.data(), node.name.size()) &&
         add_float_array(document, value, "translation",
                         gneiss::engine::math::components(node.local_transform.translation).data(),
                         3U) &&
         add_float_array(document, value, "rotation",
                         gneiss::engine::math::components(node.local_transform.rotation).data(),
                         4U) &&
         add_float_array(document, value, "scale",
                         gneiss::engine::math::components(node.local_transform.scale).data(), 3U) &&
         add_components(document, value, node.components) &&
         yyjson_mut_obj_add_val(document, change, "node", value);
}

bool parse_node(yyjson_val* change, gneiss::host::ipc_inspection_node& output) {
  auto* node = yyjson_obj_get(change, "node");
  auto* uuid = yyjson_is_obj(node) ? yyjson_obj_get(node, "uuid") : nullptr;
  auto* prefab_instance_uuid =
      yyjson_is_obj(node) ? yyjson_obj_get(node, "prefab_instance_uuid") : nullptr;
  auto* prefab_source_node_uuid =
      yyjson_is_obj(node) ? yyjson_obj_get(node, "prefab_source_node_uuid") : nullptr;
  auto* name = yyjson_is_obj(node) ? yyjson_obj_get(node, "name") : nullptr;
  if (!yyjson_is_str(uuid) || yyjson_get_len(uuid) == 0U ||
      yyjson_get_len(uuid) > max_string_size || !yyjson_is_str(name) ||
      !yyjson_is_str(prefab_instance_uuid) ||
      yyjson_get_len(prefab_instance_uuid) > max_string_size ||
      !yyjson_is_str(prefab_source_node_uuid) ||
      yyjson_get_len(prefab_source_node_uuid) > max_string_size ||
      yyjson_get_len(name) > max_string_size || !parse_id(node, "id", false, output.id) ||
      !parse_id(node, "parent", true, output.parent) ||
      !parse_float_array(
          node, "translation",
          gneiss::engine::math::components(output.local_transform.translation).data(), 3U) ||
      !parse_float_array(node, "rotation",
                         gneiss::engine::math::components(output.local_transform.rotation).data(),
                         4U) ||
      !parse_float_array(node, "scale",
                         gneiss::engine::math::components(output.local_transform.scale).data(),
                         3U)) {
    return false;
  }
  if (!parse_uuid(uuid, false, output.uuid) ||
      !parse_uuid(prefab_instance_uuid, true, output.prefab_instance_uuid) ||
      !parse_uuid(prefab_source_node_uuid, true, output.prefab_source_node_uuid) ||
      (output.prefab_source_node_uuid && !output.prefab_instance_uuid))
    return false;
  output.name.assign(yyjson_get_str(name), yyjson_get_len(name));
  return parse_components(node, output.components);
}

} // namespace

namespace gneiss::host {

gneiss::engine::error encode_ipc_inspection_batch(const ipc_inspection_batch& batch,
                                                  std::vector<std::uint8_t>& output) noexcept {
  if (batch.stamp.session_id == 0U || batch.stamp.sequence == 0U ||
      batch.changes.size() > max_changes || batch.chunk_count == 0U ||
      batch.chunk_count > max_chunks || batch.chunk_index >= batch.chunk_count) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* changes = document ? yyjson_mut_arr(document.get()) : nullptr;
    if (root == nullptr || changes == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", batch.stamp.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "sequence", batch.stamp.sequence) ||
        !yyjson_mut_obj_add_bool(document.get(), root, "full", batch.is_full)) {
      return gneiss::engine::error::out_of_memory;
    }
    if (!yyjson_mut_obj_add_uint(document.get(), root, "chunk_index", batch.chunk_index) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "chunk_count", batch.chunk_count)) {
      return gneiss::engine::error::out_of_memory;
    }
    for (const auto& change : batch.changes) {
      auto* object = yyjson_mut_obj(document.get());
      const auto* operation =
          change.type == ipc_inspection_change_type::upsert ? "upsert" : "remove";
      if (object == nullptr || !change.id.is_valid() ||
          !yyjson_mut_obj_add_str(document.get(), object, "operation", operation) ||
          !add_id(document.get(), object, "id", change.id) ||
          (change.type == ipc_inspection_change_type::upsert &&
           (change.node.id != change.id || change.node.uuid.is_nil() ||
            (change.node.prefab_source_node_uuid && !change.node.prefab_instance_uuid) ||
            change.node.name.size() > max_string_size ||
            !add_node(document.get(), object, change.node))) ||
          !yyjson_mut_arr_add_val(changes, object)) {
        return gneiss::engine::error::invalid_argument;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "changes", changes)) {
      return gneiss::engine::error::out_of_memory;
    }
    yyjson_mut_doc_set_root(document.get(), root);
    std::size_t length = 0U;
    std::unique_ptr<char, decltype(&std::free)> json(
        yyjson_mut_write(document.get(), YYJSON_WRITE_NOFLAG, &length), &std::free);
    if (!json) {
      return gneiss::engine::error::out_of_memory;
    }
    if (length > max_json_size) {
      return gneiss::engine::error::invalid_argument;
    }
    std::vector<std::uint8_t> encoded(reinterpret_cast<const std::uint8_t*>(json.get()),
                                      reinterpret_cast<const std::uint8_t*>(json.get()) + length);
    output = std::move(encoded);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error
encode_ipc_inspection_batch_chunks(const ipc_inspection_batch& batch,
                                   std::vector<std::vector<std::uint8_t>>& output) noexcept {
  if (batch.stamp.session_id == 0U || batch.stamp.sequence == 0U ||
      batch.changes.size() > max_changes) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    std::vector<ipc_inspection_batch> chunks(1U);
    chunks.front().stamp = batch.stamp;
    chunks.front().is_full = batch.is_full;
    for (const auto& change : batch.changes) {
      auto& current = chunks.back();
      current.changes.push_back(change);
      current.chunk_count = max_outgoing_chunks;
      std::vector<std::uint8_t> probe;
      if (encode_ipc_inspection_batch(current, probe) == gneiss::engine::error::success) {
        continue;
      }
      current.changes.pop_back();
      if (current.changes.empty() || chunks.size() >= max_outgoing_chunks) {
        return gneiss::engine::error::out_of_memory;
      }
      chunks.push_back({.stamp = batch.stamp, .is_full = batch.is_full, .changes = {change}});
      chunks.back().chunk_count = max_outgoing_chunks;
      if (encode_ipc_inspection_batch(chunks.back(), probe) != gneiss::engine::error::success) {
        return gneiss::engine::error::out_of_memory;
      }
    }
    std::vector<std::vector<std::uint8_t>> payloads;
    payloads.reserve(chunks.size());
    for (std::size_t index = 0U; index < chunks.size(); ++index) {
      chunks[index].chunk_index = static_cast<std::uint32_t>(index);
      chunks[index].chunk_count = static_cast<std::uint32_t>(chunks.size());
      std::vector<std::uint8_t> payload;
      const auto encoded = encode_ipc_inspection_batch(chunks[index], payload);
      if (encoded != gneiss::engine::error::success) {
        return encoded;
      }
      payloads.push_back(std::move(payload));
    }
    output = std::move(payloads);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error decode_ipc_inspection_batch(std::span<const std::uint8_t> payload,
                                                  ipc_inspection_batch& output) noexcept {
  if (payload.size() > max_json_size) {
    return gneiss::engine::error::invalid_argument;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    auto* session = yyjson_is_obj(root) ? yyjson_obj_get(root, "session_id") : nullptr;
    auto* sequence = yyjson_is_obj(root) ? yyjson_obj_get(root, "sequence") : nullptr;
    auto* full = yyjson_is_obj(root) ? yyjson_obj_get(root, "full") : nullptr;
    auto* chunk_index = yyjson_is_obj(root) ? yyjson_obj_get(root, "chunk_index") : nullptr;
    auto* chunk_count = yyjson_is_obj(root) ? yyjson_obj_get(root, "chunk_count") : nullptr;
    auto* changes = yyjson_is_obj(root) ? yyjson_obj_get(root, "changes") : nullptr;
    if (!yyjson_is_uint(session) || yyjson_get_uint(session) == 0U || !yyjson_is_uint(sequence) ||
        yyjson_get_uint(sequence) == 0U || !yyjson_is_bool(full) || !yyjson_is_uint(chunk_index) ||
        !yyjson_is_uint(chunk_count) || yyjson_get_uint(chunk_count) == 0U ||
        yyjson_get_uint(chunk_count) > max_chunks ||
        yyjson_get_uint(chunk_index) >= yyjson_get_uint(chunk_count) || !yyjson_is_arr(changes) ||
        yyjson_arr_size(changes) > max_changes) {
      return gneiss::engine::error::invalid_argument;
    }
    ipc_inspection_batch pending;
    pending.stamp = {yyjson_get_uint(session), yyjson_get_uint(sequence)};
    pending.is_full = yyjson_get_bool(full);
    pending.chunk_index = static_cast<std::uint32_t>(yyjson_get_uint(chunk_index));
    pending.chunk_count = static_cast<std::uint32_t>(yyjson_get_uint(chunk_count));
    std::set<std::pair<std::uint64_t, std::uint32_t>> seen;
    std::size_t index = 0U;
    std::size_t count = 0U;
    yyjson_val* value = nullptr;
    yyjson_arr_foreach(changes, index, count, value) {
      auto* operation = yyjson_is_obj(value) ? yyjson_obj_get(value, "operation") : nullptr;
      ipc_inspection_change change;
      if (!yyjson_is_str(operation) || !parse_id(value, "id", false, change.id) ||
          !seen.emplace(change.id.value, change.id.generation).second) {
        return gneiss::engine::error::invalid_argument;
      }
      const std::string_view name(yyjson_get_str(operation), yyjson_get_len(operation));
      if (name == "upsert") {
        change.type = ipc_inspection_change_type::upsert;
        if (!parse_node(value, change.node) || change.node.id != change.id) {
          return gneiss::engine::error::invalid_argument;
        }
      } else if (name == "remove") {
        change.type = ipc_inspection_change_type::remove;
      } else {
        return gneiss::engine::error::invalid_argument;
      }
      pending.changes.push_back(std::move(change));
    }
    output = std::move(pending);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

namespace {

constexpr auto inspection_event_kind =
    gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event);
constexpr auto inspection_request_kind =
    gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request);
constexpr std::array inspection_operations{
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_inspection_operation::snapshot),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = inspection_event_kind},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_inspection_operation::resync),
        .editor_to_runtime_kinds = inspection_request_kind,
        .runtime_to_editor_kinds = 0U}};

[[nodiscard]] bool matches_inspection(const gneiss::engine::ipc_envelope& envelope,
                                      ipc_inspection_operation operation,
                                      gneiss::engine::ipc_message_kind kind) noexcept {
  return envelope.domain == gneiss::engine::ipc_domain::inspection &&
         envelope.operation == static_cast<std::uint16_t>(operation) && envelope.kind == kind;
}

} // namespace

gneiss::engine::error
encode_ipc_inspection_batch_v2(const ipc_inspection_batch& batch,
                               std::vector<gneiss::engine::ipc_envelope>& output) noexcept {
  std::vector<std::vector<std::uint8_t>> payloads;
  auto operation = encode_ipc_inspection_batch_chunks(batch, payloads);
  if (operation != gneiss::engine::error::success) {
    return operation;
  }
  try {
    std::vector<gneiss::engine::ipc_envelope> encoded;
    encoded.reserve(payloads.size());
    for (auto& payload : payloads) {
      encoded.push_back(
          {.domain = gneiss::engine::ipc_domain::inspection,
           .operation = static_cast<std::uint16_t>(ipc_inspection_operation::snapshot),
           .kind = gneiss::engine::ipc_message_kind::event,
           .request_id = 0U,
           .payload = std::move(payload)});
    }
    output = std::move(encoded);
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  } catch (...) {
    return gneiss::engine::error::internal;
  }
}

gneiss::engine::error decode_ipc_inspection_batch_v2(const gneiss::engine::ipc_envelope& envelope,
                                                     ipc_inspection_batch& output) noexcept {
  if (!matches_inspection(envelope, ipc_inspection_operation::snapshot,
                          gneiss::engine::ipc_message_kind::event) ||
      envelope.request_id != 0U) {
    return gneiss::engine::error::invalid_argument;
  }
  return decode_ipc_inspection_batch(envelope.payload, output);
}

gneiss::engine::error encode_ipc_inspection_resync(std::uint32_t request_id,
                                                   gneiss::engine::ipc_envelope& output) noexcept {
  if (request_id == 0U) {
    return gneiss::engine::error::invalid_argument;
  }
  output = {.domain = gneiss::engine::ipc_domain::inspection,
            .operation = static_cast<std::uint16_t>(ipc_inspection_operation::resync),
            .kind = gneiss::engine::ipc_message_kind::request,
            .request_id = request_id,
            .payload = {}};
  return gneiss::engine::error::success;
}

gneiss::engine::error
decode_ipc_inspection_resync(const gneiss::engine::ipc_envelope& envelope) noexcept {
  return matches_inspection(envelope, ipc_inspection_operation::resync,
                            gneiss::engine::ipc_message_kind::request) &&
                 envelope.request_id != 0U && envelope.payload.empty()
             ? gneiss::engine::error::success
             : gneiss::engine::error::invalid_argument;
}

std::span<const gneiss::engine::ipc_operation_descriptor> ipc_inspection_operations() noexcept {
  return inspection_operations;
}

} // namespace gneiss::host
