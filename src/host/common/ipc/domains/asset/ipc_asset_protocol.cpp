// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_asset_protocol.hpp"
#include "engine/core/result.hpp"

#include <yyjson.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <set>
#include <string_view>
#include <utility>

namespace gneiss::host {
namespace {

constexpr std::size_t max_asset_count = 1024U;
constexpr std::size_t max_uri_size = 2048U;
constexpr std::size_t max_message_size = 4096U;
using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

[[nodiscard]] std::string_view asset_type_name(ipc_asset_type type) noexcept {
  switch (type) {
  case ipc_asset_type::texture:
    return "texture";
  case ipc_asset_type::material:
    return "material";
  case ipc_asset_type::static_mesh:
    return "static_mesh";
  case ipc_asset_type::scene:
    return "scene";
  case ipc_asset_type::prefab:
    return "prefab";
  }
  return {};
}

[[nodiscard]] std::string_view status_name(ipc_asset_apply_status status) noexcept {
  switch (status) {
  case ipc_asset_apply_status::preparing:
    return "preparing";
  case ipc_asset_apply_status::uploading:
    return "uploading";
  case ipc_asset_apply_status::cancelled:
    return "cancelled";
  case ipc_asset_apply_status::applied:
    return "applied";
  case ipc_asset_apply_status::failed:
    return "failed";
  case ipc_asset_apply_status::stale:
    return "stale";
  case ipc_asset_apply_status::restart_required:
    return "restart_required";
  }
  return {};
}

[[nodiscard]] bool parse_asset_type(std::string_view text, ipc_asset_type& output) noexcept {
  if (text == "texture") {
    output = ipc_asset_type::texture;
  } else if (text == "material") {
    output = ipc_asset_type::material;
  } else if (text == "static_mesh") {
    output = ipc_asset_type::static_mesh;
  } else if (text == "scene") {
    output = ipc_asset_type::scene;
  } else if (text == "prefab") {
    output = ipc_asset_type::prefab;
  } else {
    return false;
  }
  return true;
}

[[nodiscard]] bool valid_asset_group(std::span<const ipc_asset_revision> assets) noexcept {
  if (assets.empty()) {
    return false;
  }
  const bool structural =
      assets.front().type == ipc_asset_type::scene || assets.front().type == ipc_asset_type::prefab;
  return std::ranges::all_of(assets, [&](const auto& asset) {
    if (!structural) {
      return asset.type == ipc_asset_type::texture || asset.type == ipc_asset_type::material ||
             asset.type == ipc_asset_type::static_mesh;
    }
    return asset.type == assets.front().type;
  });
}

[[nodiscard]] bool parse_status(std::string_view text, ipc_asset_apply_status& output) noexcept {
  if (text == "preparing") {
    output = ipc_asset_apply_status::preparing;
  } else if (text == "uploading") {
    output = ipc_asset_apply_status::uploading;
  } else if (text == "cancelled") {
    output = ipc_asset_apply_status::cancelled;
  } else if (text == "applied") {
    output = ipc_asset_apply_status::applied;
  } else if (text == "failed") {
    output = ipc_asset_apply_status::failed;
  } else if (text == "stale") {
    output = ipc_asset_apply_status::stale;
  } else if (text == "restart_required") {
    output = ipc_asset_apply_status::restart_required;
  } else {
    return false;
  }
  return true;
}

[[nodiscard]] bool valid_uri(std::string_view uri) noexcept {
  return uri.starts_with("asset://") && uri.size() <= max_uri_size &&
         uri.find("..") == std::string_view::npos;
}

[[nodiscard]] gneiss::engine::result write_document(yyjson_mut_doc* document, yyjson_mut_val* root,
                                                    std::vector<std::uint8_t>& output) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> text(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!text) {
    return gneiss::engine::result::out_of_memory;
  }
  if (length > ipc_asset_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  output.assign(reinterpret_cast<const std::uint8_t*>(text.get()),
                reinterpret_cast<const std::uint8_t*>(text.get()) + length);
  return gneiss::engine::result::success;
}

[[nodiscard]] bool parse_header(yyjson_val* root, std::uint64_t& session_id,
                                std::uint64_t& revision) noexcept {
  auto* session = yyjson_is_obj(root) ? yyjson_obj_get(root, "session_id") : nullptr;
  auto* revision_value = yyjson_is_obj(root) ? yyjson_obj_get(root, "revision") : nullptr;
  if (!yyjson_is_uint(session) || !yyjson_is_uint(revision_value)) {
    return false;
  }
  session_id = yyjson_get_uint(session);
  revision = yyjson_get_uint(revision_value);
  return session_id != 0U && revision != 0U;
}

[[nodiscard]] bool known_operation(std::uint16_t operation) noexcept {
  return operation == static_cast<std::uint16_t>(ipc_asset_operation::reload) ||
         operation == static_cast<std::uint16_t>(ipc_asset_operation::resync);
}

} // namespace

gneiss::engine::result encode_ipc_asset_request(const ipc_asset_reload_request& request,
                                                std::vector<std::uint8_t>& output) noexcept {
  if (request.session_id == 0U || request.revision == 0U || !valid_asset_group(request.assets) ||
      request.assets.size() > max_asset_count) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    std::set<std::string> uris;
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* assets = document ? yyjson_mut_arr(document.get()) : nullptr;
    if (root == nullptr || assets == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", request.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", request.revision)) {
      return gneiss::engine::result::out_of_memory;
    }
    for (const auto& asset : request.assets) {
      const auto type = asset_type_name(asset.type);
      if (!valid_uri(asset.uri) || type.empty() || !uris.insert(asset.uri).second) {
        return gneiss::engine::result::invalid_argument;
      }
      auto* item = yyjson_mut_obj(document.get());
      if (item == nullptr ||
          !yyjson_mut_obj_add_strncpy(document.get(), item, "uri", asset.uri.data(),
                                      asset.uri.size()) ||
          !yyjson_mut_obj_add_strncpy(document.get(), item, "type", type.data(), type.size()) ||
          !yyjson_mut_arr_add_val(assets, item)) {
        return gneiss::engine::result::out_of_memory;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "assets", assets)) {
      return gneiss::engine::result::out_of_memory;
    }
    return write_document(document.get(), root, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_asset_request(std::span<const std::uint8_t> payload,
                                                ipc_asset_reload_request& output) noexcept {
  if (payload.size() > ipc_asset_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    ipc_asset_reload_request parsed;
    auto* assets = yyjson_is_obj(root) ? yyjson_obj_get(root, "assets") : nullptr;
    if (!parse_header(root, parsed.session_id, parsed.revision) || !yyjson_is_arr(assets) ||
        yyjson_arr_size(assets) == 0U || yyjson_arr_size(assets) > max_asset_count) {
      return gneiss::engine::result::invalid_argument;
    }
    std::set<std::string> uris;
    std::size_t index = 0U;
    std::size_t count = 0U;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(assets, index, count, item) {
      auto* uri = yyjson_is_obj(item) ? yyjson_obj_get(item, "uri") : nullptr;
      auto* type = yyjson_is_obj(item) ? yyjson_obj_get(item, "type") : nullptr;
      ipc_asset_revision revision;
      if (!yyjson_is_str(uri) || !yyjson_is_str(type) || yyjson_get_len(uri) > max_uri_size) {
        return gneiss::engine::result::invalid_argument;
      }
      revision.uri.assign(yyjson_get_str(uri), yyjson_get_len(uri));
      const std::string_view type_text(yyjson_get_str(type), yyjson_get_len(type));
      if (!valid_uri(revision.uri) || !parse_asset_type(type_text, revision.type) ||
          !uris.insert(revision.uri).second) {
        return gneiss::engine::result::invalid_argument;
      }
      parsed.assets.push_back(std::move(revision));
    }
    if (!valid_asset_group(parsed.assets)) {
      return gneiss::engine::result::invalid_argument;
    }
    output = std::move(parsed);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result encode_ipc_asset_result(const ipc_asset_reload_result& response,
                                               std::vector<std::uint8_t>& output) noexcept {
  const auto status = status_name(response.status);
  if (response.session_id == 0U || response.revision == 0U || status.empty() ||
      response.message.size() > max_message_size ||
      response.completed_assets > response.total_assets || response.total_assets > 256U ||
      (response.can_cancel && response.status != ipc_asset_apply_status::preparing)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (root == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", response.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", response.revision) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "status", status.data(), status.size()) ||
        !yyjson_mut_obj_add_bool(document.get(), root, "can_cancel", response.can_cancel) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "completed_assets",
                                 response.completed_assets) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "total_assets", response.total_assets) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", response.message.data(),
                                    response.message.size())) {
      return gneiss::engine::result::out_of_memory;
    }
    if (response.items.size() > 256U) {
      return gneiss::engine::result::invalid_argument;
    }
    auto* items = yyjson_mut_arr(document.get());
    if (!items || !yyjson_mut_obj_add_val(document.get(), root, "items", items)) {
      return gneiss::engine::result::out_of_memory;
    }
    std::set<std::string> uris;
    for (const auto& value : response.items) {
      const auto type = asset_type_name(value.asset.type);
      if (!valid_uri(value.asset.uri) || type.empty() || !uris.insert(value.asset.uri).second ||
          (value.applied && value.operation != gneiss::engine::result::success) ||
          value.operation.code() > gneiss::engine::result::success.code() ||
          value.operation.code() < gneiss::engine::result::io.code()) {
        return gneiss::engine::result::invalid_argument;
      }
      auto* item = yyjson_mut_obj(document.get());
      if (!item ||
          !yyjson_mut_obj_add_strncpy(document.get(), item, "uri", value.asset.uri.data(),
                                      value.asset.uri.size()) ||
          !yyjson_mut_obj_add_strncpy(document.get(), item, "type", type.data(), type.size()) ||
          !yyjson_mut_obj_add_int(document.get(), item, "result", value.operation.code()) ||
          !yyjson_mut_obj_add_bool(document.get(), item, "applied", value.applied) ||
          !yyjson_mut_arr_add_val(items, item)) {
        return gneiss::engine::result::out_of_memory;
      }
    }
    return write_document(document.get(), root, output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result decode_ipc_asset_result(std::span<const std::uint8_t> payload,
                                               ipc_asset_reload_result& output) noexcept {
  if (payload.size() > ipc_asset_max_payload_size) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(payload.data()), payload.size(),
                                      YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    ipc_asset_reload_result parsed;
    auto* status = yyjson_is_obj(root) ? yyjson_obj_get(root, "status") : nullptr;
    auto* message = yyjson_is_obj(root) ? yyjson_obj_get(root, "message") : nullptr;
    if (!parse_header(root, parsed.session_id, parsed.revision) || !yyjson_is_str(status) ||
        !yyjson_is_str(message) || yyjson_get_len(message) > max_message_size ||
        !parse_status({yyjson_get_str(status), yyjson_get_len(status)}, parsed.status)) {
      return gneiss::engine::result::invalid_argument;
    }
    auto* cancel = yyjson_obj_get(root, "can_cancel");
    auto* completed = yyjson_obj_get(root, "completed_assets");
    auto* total = yyjson_obj_get(root, "total_assets");
    if (!yyjson_is_bool(cancel) || !yyjson_is_uint(completed) || !yyjson_is_uint(total) ||
        yyjson_get_uint(total) > 256U || yyjson_get_uint(completed) > yyjson_get_uint(total) ||
        (yyjson_get_bool(cancel) && parsed.status != ipc_asset_apply_status::preparing)) {
      return gneiss::engine::result::invalid_argument;
    }
    parsed.can_cancel = yyjson_get_bool(cancel);
    parsed.completed_assets = static_cast<std::uint32_t>(yyjson_get_uint(completed));
    parsed.total_assets = static_cast<std::uint32_t>(yyjson_get_uint(total));
    parsed.message.assign(yyjson_get_str(message), yyjson_get_len(message));
    auto* items = yyjson_obj_get(root, "items");
    if (!yyjson_is_arr(items) || yyjson_arr_size(items) > 256U) {
      return gneiss::engine::result::invalid_argument;
    }
    std::set<std::string> uris;
    std::size_t index{}, count{};
    yyjson_val* item{};
    yyjson_arr_foreach(items, index, count, item) {
      auto* uri = yyjson_obj_get(item, "uri");
      auto* type = yyjson_obj_get(item, "type");
      auto* code = yyjson_obj_get(item, "result");
      auto* applied = yyjson_obj_get(item, "applied");
      if (!yyjson_is_str(uri) || !yyjson_is_str(type) || !yyjson_is_int(code) ||
          !yyjson_is_bool(applied) ||
          yyjson_get_sint(code) > gneiss::engine::result::success.code() ||
          yyjson_get_sint(code) < gneiss::engine::result::io.code()) {
        return gneiss::engine::result::invalid_argument;
      }
      ipc_asset_item_result value;
      value.asset.uri.assign(yyjson_get_str(uri), yyjson_get_len(uri));
      value.operation = gneiss::engine::result{static_cast<std::int32_t>(yyjson_get_sint(code))};
      value.applied = yyjson_get_bool(applied);
      if (!valid_uri(value.asset.uri) || !uris.insert(value.asset.uri).second ||
          !parse_asset_type({yyjson_get_str(type), yyjson_get_len(type)}, value.asset.type) ||
          (value.applied && value.operation != gneiss::engine::result::success)) {
        return gneiss::engine::result::invalid_argument;
      }
      parsed.items.push_back(std::move(value));
    }
    output = std::move(parsed);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

namespace {

constexpr auto request_kind =
    gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::request);
constexpr auto response_kind =
    gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::response);
constexpr std::array asset_operations{
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_asset_operation::progress),
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event)},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_asset_operation::cancel),
        .editor_to_runtime_kinds =
            gneiss::engine::ipc_kind_mask(gneiss::engine::ipc_message_kind::event),
        .runtime_to_editor_kinds = 0U},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_asset_operation::reload),
        .editor_to_runtime_kinds = request_kind,
        .runtime_to_editor_kinds = response_kind},
    gneiss::engine::ipc_operation_descriptor{
        .operation = static_cast<std::uint16_t>(ipc_asset_operation::resync),
        .editor_to_runtime_kinds = request_kind,
        .runtime_to_editor_kinds = response_kind}};

} // namespace

gneiss::engine::result encode_ipc_asset_cancel(std::uint64_t session, std::uint64_t revision,
                                               gneiss::engine::ipc_envelope& output) noexcept try {
  if (session == 0U || revision == 0U)
    return gneiss::engine::result::invalid_argument;
  mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
  auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
  if (!root || !yyjson_mut_obj_add_uint(document.get(), root, "session_id", session) ||
      !yyjson_mut_obj_add_uint(document.get(), root, "revision", revision))
    return gneiss::engine::result::out_of_memory;
  std::vector<std::uint8_t> payload;
  auto result = write_document(document.get(), root, payload);
  if (result != gneiss::engine::result::success)
    return result;
  output = {.domain = gneiss::engine::ipc_domain::asset,
            .operation = static_cast<std::uint16_t>(ipc_asset_operation::cancel),
            .kind = gneiss::engine::ipc_message_kind::event,
            .request_id = 0U,
            .payload = std::move(payload)};
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result decode_ipc_asset_cancel(const gneiss::engine::ipc_envelope& envelope,
                                               ipc_asset_reload_request& output) noexcept {
  if (envelope.domain != gneiss::engine::ipc_domain::asset ||
      envelope.operation != static_cast<std::uint16_t>(ipc_asset_operation::cancel) ||
      envelope.kind != gneiss::engine::ipc_message_kind::event || envelope.request_id != 0U ||
      envelope.payload.size() > 256U)
    return gneiss::engine::result::invalid_argument;
  document_ptr document(yyjson_read(reinterpret_cast<const char*>(envelope.payload.data()),
                                    envelope.payload.size(), YYJSON_READ_NOFLAG),
                        &yyjson_doc_free);
  ipc_asset_reload_request parsed;
  if (!document ||
      !parse_header(yyjson_doc_get_root(document.get()), parsed.session_id, parsed.revision))
    return gneiss::engine::result::invalid_argument;
  output = std::move(parsed);
  return gneiss::engine::result::success;
}

gneiss::engine::result encode_ipc_asset_request_v2(const ipc_asset_reload_request& request,
                                                   ipc_asset_operation operation,
                                                   std::uint32_t request_id,
                                                   gneiss::engine::ipc_envelope& output) noexcept {
  if (request_id == 0U || !known_operation(static_cast<std::uint16_t>(operation))) {
    return gneiss::engine::result::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto encoded = encode_ipc_asset_request(request, payload);
  if (encoded != gneiss::engine::result::success) {
    return encoded;
  }
  output = {.domain = gneiss::engine::ipc_domain::asset,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = gneiss::engine::ipc_message_kind::request,
            .request_id = request_id,
            .payload = std::move(payload)};
  return gneiss::engine::result::success;
}

gneiss::engine::result decode_ipc_asset_request_v2(const gneiss::engine::ipc_envelope& envelope,
                                                   ipc_asset_reload_request& output) noexcept {
  if (envelope.domain != gneiss::engine::ipc_domain::asset ||
      !known_operation(envelope.operation) ||
      envelope.kind != gneiss::engine::ipc_message_kind::request || envelope.request_id == 0U) {
    return gneiss::engine::result::invalid_argument;
  }
  return decode_ipc_asset_request(envelope.payload, output);
}

gneiss::engine::result encode_ipc_asset_result_v2(const ipc_asset_reload_result& response,
                                                  ipc_asset_operation operation,
                                                  std::uint32_t request_id,
                                                  gneiss::engine::ipc_envelope& output) noexcept {
  const bool progress = operation == ipc_asset_operation::progress;
  if (progress ? (request_id != 0U || (response.status != ipc_asset_apply_status::preparing &&
                                       response.status != ipc_asset_apply_status::uploading))
               : (request_id == 0U || !known_operation(static_cast<std::uint16_t>(operation)) ||
                  response.status == ipc_asset_apply_status::preparing ||
                  response.status == ipc_asset_apply_status::uploading)) {
    return gneiss::engine::result::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto encoded = encode_ipc_asset_result(response, payload);
  if (encoded != gneiss::engine::result::success) {
    return encoded;
  }
  output = {.domain = gneiss::engine::ipc_domain::asset,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = progress ? gneiss::engine::ipc_message_kind::event
                             : gneiss::engine::ipc_message_kind::response,
            .request_id = request_id,
            .payload = std::move(payload)};
  return gneiss::engine::result::success;
}

gneiss::engine::result decode_ipc_asset_result_v2(const gneiss::engine::ipc_envelope& envelope,
                                                  ipc_asset_reload_result& output) noexcept {
  const bool progress =
      envelope.operation == static_cast<std::uint16_t>(ipc_asset_operation::progress);
  if (envelope.domain != gneiss::engine::ipc_domain::asset ||
      (progress
           ? envelope.kind != gneiss::engine::ipc_message_kind::event || envelope.request_id != 0U
           : !known_operation(envelope.operation) ||
                 envelope.kind != gneiss::engine::ipc_message_kind::response ||
                 envelope.request_id == 0U))
    return gneiss::engine::result::invalid_argument;
  ipc_asset_reload_result parsed;
  const auto decoded = decode_ipc_asset_result(envelope.payload, parsed);
  if (decoded != gneiss::engine::result::success)
    return decoded;
  if (progress != (parsed.status == ipc_asset_apply_status::preparing ||
                   parsed.status == ipc_asset_apply_status::uploading))
    return gneiss::engine::result::invalid_argument;
  output = std::move(parsed);
  return gneiss::engine::result::success;
}

std::span<const gneiss::engine::ipc_operation_descriptor> ipc_asset_operations() noexcept {
  return asset_operations;
}

} // namespace gneiss::host
