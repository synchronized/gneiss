// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_asset_protocol.h"

#include <yyjson.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <set>
#include <string_view>
#include <utility>

namespace gneiss {
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
  }
  return {};
}

[[nodiscard]] std::string_view status_name(ipc_asset_apply_status status) noexcept {
  switch (status) {
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
  } else {
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_status(std::string_view text, ipc_asset_apply_status& output) noexcept {
  if (text == "applied") {
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

[[nodiscard]] result write_document(yyjson_mut_doc* document, yyjson_mut_val* root,
                                    std::vector<std::uint8_t>& output) {
  yyjson_mut_doc_set_root(document, root);
  std::size_t length = 0U;
  std::unique_ptr<char, decltype(&std::free)> text(
      yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, &length), &std::free);
  if (!text) {
    return result::out_of_memory;
  }
  if (length > ipc_asset_max_payload_size) {
    return result::invalid_argument;
  }
  output.assign(reinterpret_cast<const std::uint8_t*>(text.get()),
                reinterpret_cast<const std::uint8_t*>(text.get()) + length);
  return result::success;
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

result encode_ipc_asset_request(const ipc_asset_reload_request& request,
                                std::vector<std::uint8_t>& output) noexcept {
  if (request.session_id == 0U || request.revision == 0U || request.assets.empty() ||
      request.assets.size() > max_asset_count) {
    return result::invalid_argument;
  }
  try {
    std::set<std::string> uris;
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    auto* assets = document ? yyjson_mut_arr(document.get()) : nullptr;
    if (root == nullptr || assets == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", request.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", request.revision)) {
      return result::out_of_memory;
    }
    for (const auto& asset : request.assets) {
      const auto type = asset_type_name(asset.type);
      if (!valid_uri(asset.uri) || type.empty() || !uris.insert(asset.uri).second) {
        return result::invalid_argument;
      }
      auto* item = yyjson_mut_obj(document.get());
      if (item == nullptr ||
          !yyjson_mut_obj_add_strncpy(document.get(), item, "uri", asset.uri.data(),
                                      asset.uri.size()) ||
          !yyjson_mut_obj_add_strncpy(document.get(), item, "type", type.data(), type.size()) ||
          !yyjson_mut_arr_add_val(assets, item)) {
        return result::out_of_memory;
      }
    }
    if (!yyjson_mut_obj_add_val(document.get(), root, "assets", assets)) {
      return result::out_of_memory;
    }
    return write_document(document.get(), root, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_asset_request(std::span<const std::uint8_t> payload,
                                ipc_asset_reload_request& output) noexcept {
  if (payload.size() > ipc_asset_max_payload_size) {
    return result::invalid_argument;
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
      return result::invalid_argument;
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
        return result::invalid_argument;
      }
      revision.uri.assign(yyjson_get_str(uri), yyjson_get_len(uri));
      const std::string_view type_text(yyjson_get_str(type), yyjson_get_len(type));
      if (!valid_uri(revision.uri) || !parse_asset_type(type_text, revision.type) ||
          !uris.insert(revision.uri).second) {
        return result::invalid_argument;
      }
      parsed.assets.push_back(std::move(revision));
    }
    output = std::move(parsed);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result encode_ipc_asset_result(const ipc_asset_reload_result& response,
                               std::vector<std::uint8_t>& output) noexcept {
  const auto status = status_name(response.status);
  if (response.session_id == 0U || response.revision == 0U || status.empty() ||
      response.message.size() > max_message_size) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    auto* root = document ? yyjson_mut_obj(document.get()) : nullptr;
    if (root == nullptr ||
        !yyjson_mut_obj_add_uint(document.get(), root, "session_id", response.session_id) ||
        !yyjson_mut_obj_add_uint(document.get(), root, "revision", response.revision) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "status", status.data(), status.size()) ||
        !yyjson_mut_obj_add_strncpy(document.get(), root, "message", response.message.data(),
                                    response.message.size())) {
      return result::out_of_memory;
    }
    return write_document(document.get(), root, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_asset_result(std::span<const std::uint8_t> payload,
                               ipc_asset_reload_result& output) noexcept {
  if (payload.size() > ipc_asset_max_payload_size) {
    return result::invalid_argument;
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
      return result::invalid_argument;
    }
    parsed.message.assign(yyjson_get_str(message), yyjson_get_len(message));
    output = std::move(parsed);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

namespace {

constexpr auto request_kind = ipc_kind_mask(ipc_message_kind::request);
constexpr auto response_kind = ipc_kind_mask(ipc_message_kind::response);
constexpr std::array asset_operations{
    ipc_operation_descriptor{.operation = static_cast<std::uint16_t>(ipc_asset_operation::reload),
                             .editor_to_runtime_kinds = request_kind,
                             .runtime_to_editor_kinds = response_kind},
    ipc_operation_descriptor{.operation = static_cast<std::uint16_t>(ipc_asset_operation::resync),
                             .editor_to_runtime_kinds = request_kind,
                             .runtime_to_editor_kinds = response_kind}};

} // namespace

result encode_ipc_asset_request_v2(const ipc_asset_reload_request& request,
                                   ipc_asset_operation operation, std::uint32_t request_id,
                                   ipc_envelope& output) noexcept {
  if (request_id == 0U || !known_operation(static_cast<std::uint16_t>(operation))) {
    return result::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto encoded = encode_ipc_asset_request(request, payload);
  if (encoded != result::success) {
    return encoded;
  }
  output = {.domain = ipc_domain::asset,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = ipc_message_kind::request,
            .request_id = request_id,
            .payload = std::move(payload)};
  return result::success;
}

result decode_ipc_asset_request_v2(const ipc_envelope& envelope,
                                   ipc_asset_reload_request& output) noexcept {
  if (envelope.domain != ipc_domain::asset || !known_operation(envelope.operation) ||
      envelope.kind != ipc_message_kind::request || envelope.request_id == 0U) {
    return result::invalid_argument;
  }
  return decode_ipc_asset_request(envelope.payload, output);
}

result encode_ipc_asset_result_v2(const ipc_asset_reload_result& response,
                                  ipc_asset_operation operation, std::uint32_t request_id,
                                  ipc_envelope& output) noexcept {
  if (request_id == 0U || !known_operation(static_cast<std::uint16_t>(operation))) {
    return result::invalid_argument;
  }
  std::vector<std::uint8_t> payload;
  const auto encoded = encode_ipc_asset_result(response, payload);
  if (encoded != result::success) {
    return encoded;
  }
  output = {.domain = ipc_domain::asset,
            .operation = static_cast<std::uint16_t>(operation),
            .kind = ipc_message_kind::response,
            .request_id = request_id,
            .payload = std::move(payload)};
  return result::success;
}

result decode_ipc_asset_result_v2(const ipc_envelope& envelope,
                                  ipc_asset_reload_result& output) noexcept {
  if (envelope.domain != ipc_domain::asset || !known_operation(envelope.operation) ||
      envelope.kind != ipc_message_kind::response || envelope.request_id == 0U) {
    return result::invalid_argument;
  }
  return decode_ipc_asset_result(envelope.payload, output);
}

std::span<const ipc_operation_descriptor> ipc_asset_operations() noexcept {
  return asset_operations;
}

} // namespace gneiss
