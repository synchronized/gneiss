// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_scene_protocol.hpp"
#include "ipc_asset_protocol.hpp"

#include <array>
#include <cstdlib>
#include <memory>
#include <yyjson.h>

namespace gneiss {
namespace {
constexpr std::array<std::string_view, 8U> phases{
    "preparing", "assets", "verifying", "instantiating", "ready", "applied", "failed", "cancelled",
};
bool read_budget(yyjson_val* budget, ipc_scene_budget& parsed) {
  if (!yyjson_is_obj(budget)) {
    return false;
  }
  if (!yyjson_is_uint(yyjson_obj_get(budget, "candidate_logical_bytes"))) {
    return false;
  }
  parsed.candidate_logical_bytes =
      yyjson_get_uint(yyjson_obj_get(budget, "candidate_logical_bytes"));
  if (!yyjson_is_uint(yyjson_obj_get(budget, "candidate_cpu_data_bytes"))) {
    return false;
  }
  parsed.candidate_cpu_data_bytes =
      yyjson_get_uint(yyjson_obj_get(budget, "candidate_cpu_data_bytes"));
  if (!yyjson_is_uint(yyjson_obj_get(budget, "application_logical_bytes"))) {
    return false;
  }
  parsed.application_logical_bytes =
      yyjson_get_uint(yyjson_obj_get(budget, "application_logical_bytes"));
  if (!yyjson_is_uint(yyjson_obj_get(budget, "application_cpu_data_bytes"))) {
    return false;
  }
  parsed.application_cpu_data_bytes =
      yyjson_get_uint(yyjson_obj_get(budget, "application_cpu_data_bytes"));
  if (!yyjson_is_uint(yyjson_obj_get(budget, "available_bytes"))) {
    return false;
  }
  parsed.available_bytes = yyjson_get_uint(yyjson_obj_get(budget, "available_bytes"));
  if (!yyjson_is_uint(yyjson_obj_get(budget, "upload_reserved_bytes"))) {
    return false;
  }
  parsed.upload_reserved_bytes = yyjson_get_uint(yyjson_obj_get(budget, "upload_reserved_bytes"));
  if (!yyjson_is_uint(yyjson_obj_get(budget, "peak_upload_bytes"))) {
    return false;
  }
  parsed.peak_upload_bytes = yyjson_get_uint(yyjson_obj_get(budget, "peak_upload_bytes"));
  if (auto* cleanup = yyjson_obj_get(budget, "cleanup_complete")) {
    if (!yyjson_is_bool(cleanup)) {
      return false;
    }
    parsed.cleanup_complete = yyjson_get_bool(cleanup);
  }
  return true;
}
bool valid_source(const ipc_scene_request& value) {
  return value.session != 0U && value.revision != 0U && value.uri.starts_with("asset://") &&
         value.uri.size() > 8U && value.uri.size() <= 2048U &&
         value.uri.find("..") == std::string::npos && value.uri.find('\0') == std::string::npos;
}
bool valid_progress(const ipc_scene_progress& value) {
  return valid_source(value.source) && static_cast<std::size_t>(value.phase) < phases.size() &&
         value.total <= 65536U && value.completed <= value.total && value.message.size() <= 4096U &&
         (!value.can_cancel || !scene_phase_terminal(value.phase));
}
constexpr auto request_kind = ipc_kind_mask(ipc_message_kind::request);
constexpr auto event_kind = ipc_kind_mask(ipc_message_kind::event);
constexpr std::array operations{
    ipc_operation_descriptor{
        .operation = 1U,
        .editor_to_runtime_kinds = request_kind,
        .runtime_to_editor_kinds = ipc_kind_mask(ipc_message_kind::response),
    },
    ipc_operation_descriptor{
        .operation = 2U,
        .editor_to_runtime_kinds = event_kind,
        .runtime_to_editor_kinds = 0U,
    },
    ipc_operation_descriptor{
        .operation = 3U,
        .editor_to_runtime_kinds = 0U,
        .runtime_to_editor_kinds = event_kind,
    },
};
}
bool scene_phase_terminal(ipc_scene_phase phase) noexcept {
  return phase == ipc_scene_phase::applied || phase == ipc_scene_phase::failed ||
         phase == ipc_scene_phase::cancelled;
}
std::span<const ipc_operation_descriptor> ipc_scene_operations() noexcept { return operations; }
result encode_ipc_scene_request(const ipc_scene_request& value, ipc_scene_operation operation,
                                std::uint32_t request_id, ipc_envelope& output) noexcept try {
  if (!valid_source(value) ||
      (operation != ipc_scene_operation::load && operation != ipc_scene_operation::cancel) ||
      (operation == ipc_scene_operation::load ? request_id == 0U : request_id != 0U)) {
    return result::invalid_argument;
  }
  ipc_envelope envelope{
      .domain = ipc_domain::scene,
      .operation = static_cast<std::uint16_t>(operation),
      .kind = operation == ipc_scene_operation::load ? ipc_message_kind::request
                                                     : ipc_message_kind::event,
      .request_id = request_id,
      .payload = {},
  };
  const auto encoded = encode_ipc_asset_request(
      {
          .session_id = value.session,
          .revision = value.revision,
          .assets = {{.uri = value.uri, .type = ipc_asset_type::scene}},
      },
      envelope.payload);
  if (encoded == result::success) {
    output = std::move(envelope);
  }
  return encoded;
} catch (...) {
  return result::out_of_memory;
}
result decode_ipc_scene_request(const ipc_envelope& envelope, ipc_scene_request& output) noexcept
    try {
  const auto operation = static_cast<ipc_scene_operation>(envelope.operation);
  if (envelope.domain != ipc_domain::scene ||
      envelope.payload.size() > ipc_scene_max_payload_size ||
      validate_ipc_envelope(envelope) != result::success ||
      (operation == ipc_scene_operation::load ? envelope.kind != ipc_message_kind::request
                                              : operation != ipc_scene_operation::cancel ||
                                                    envelope.kind != ipc_message_kind::event)) {
    return result::invalid_argument;
  }
  ipc_asset_reload_request value;
  const auto decoded = decode_ipc_asset_request(envelope.payload, value);
  if (decoded != result::success) {
    return decoded;
  }
  if (value.assets.size() != 1U || value.assets.front().type != ipc_asset_type::scene) {
    return result::invalid_argument;
  }
  ipc_scene_request parsed{
      .session = value.session_id,
      .revision = value.revision,
      .uri = std::move(value.assets.front().uri),
  };
  if (!valid_source(parsed)) {
    return result::invalid_argument;
  }
  output = std::move(parsed);
  return result::success;
} catch (...) {
  return result::out_of_memory;
}
result encode_ipc_scene_progress(const ipc_scene_progress& value, std::uint32_t request_id,
                                 ipc_envelope& output) noexcept try {
  if (!valid_progress(value)) {
    return result::invalid_argument;
  }
  std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> doc(yyjson_mut_doc_new(nullptr),
                                                                      yyjson_mut_doc_free);
  auto* root = doc ? yyjson_mut_obj(doc.get()) : nullptr;
  const auto phase = phases[static_cast<std::size_t>(value.phase)];
  if ((root == nullptr) ||
      !yyjson_mut_obj_add_uint(doc.get(), root, "session", value.source.session) ||
      !yyjson_mut_obj_add_uint(doc.get(), root, "revision", value.source.revision) ||
      !yyjson_mut_obj_add_strncpy(doc.get(), root, "uri", value.source.uri.data(),
                                  value.source.uri.size()) ||
      !yyjson_mut_obj_add_strncpy(doc.get(), root, "phase", phase.data(), phase.size()) ||
      !yyjson_mut_obj_add_uint(doc.get(), root, "completed", value.completed) ||
      !yyjson_mut_obj_add_uint(doc.get(), root, "total", value.total) ||
      !yyjson_mut_obj_add_bool(doc.get(), root, "can_cancel", value.can_cancel) ||
      !yyjson_mut_obj_add_strncpy(doc.get(), root, "message", value.message.data(),
                                  value.message.size())) {
    return result::out_of_memory;
  }
  if (value.budget) {
    auto* budget = yyjson_mut_obj(doc.get());
    if (budget == nullptr || !yyjson_mut_obj_add_val(doc.get(), root, "budget", budget) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "candidate_logical_bytes",
                                 value.budget->candidate_logical_bytes) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "candidate_cpu_data_bytes",
                                 value.budget->candidate_cpu_data_bytes) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "application_logical_bytes",
                                 value.budget->application_logical_bytes) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "application_cpu_data_bytes",
                                 value.budget->application_cpu_data_bytes) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "available_bytes",
                                 value.budget->available_bytes) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "upload_reserved_bytes",
                                 value.budget->upload_reserved_bytes) ||
        !yyjson_mut_obj_add_uint(doc.get(), budget, "peak_upload_bytes",
                                 value.budget->peak_upload_bytes) ||
        !yyjson_mut_obj_add_bool(doc.get(), budget, "cleanup_complete",
                                 value.budget->cleanup_complete)) {
      return result::out_of_memory;
    }
  }
  yyjson_mut_doc_set_root(doc.get(), root);
  std::size_t length{};
  std::unique_ptr<char, decltype(&std::free)> text(
      yyjson_mut_write(doc.get(), YYJSON_WRITE_NOFLAG, &length), std::free);
  if (!text) {
    return result::out_of_memory;
  }
  if (length > ipc_scene_max_payload_size) {
    return result::invalid_argument;
  }
  const bool response = request_id != 0U && scene_phase_terminal(value.phase);
  ipc_envelope envelope{
      .domain = ipc_domain::scene,
      .operation = static_cast<std::uint16_t>(response ? ipc_scene_operation::load
                                                       : ipc_scene_operation::progress),
      .kind = response ? ipc_message_kind::response : ipc_message_kind::event,
      .request_id = response ? request_id : 0U,
      .payload = {},
  };
  envelope.payload.assign(reinterpret_cast<const std::uint8_t*>(text.get()),
                          reinterpret_cast<const std::uint8_t*>(text.get()) + length);
  output = std::move(envelope);
  return result::success;
} catch (...) {
  return result::out_of_memory;
}
result decode_ipc_scene_progress(const ipc_envelope& envelope, ipc_scene_progress& output) noexcept
    try {
  const bool response = envelope.operation == 1U && envelope.kind == ipc_message_kind::response;
  if (envelope.domain != ipc_domain::scene ||
      envelope.payload.size() > ipc_scene_max_payload_size ||
      validate_ipc_envelope(envelope) != result::success ||
      (!response && (envelope.operation != 3U || envelope.kind != ipc_message_kind::event))) {
    return result::invalid_argument;
  }
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(
      yyjson_read(reinterpret_cast<const char*>(envelope.payload.data()), envelope.payload.size(),
                  YYJSON_READ_NOFLAG),
      yyjson_doc_free);
  auto* root = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
  if (!yyjson_is_obj(root)) {
    return result::invalid_argument;
  }
  auto* session = yyjson_obj_get(root, "session");
  auto* revision = yyjson_obj_get(root, "revision");
  auto* uri = yyjson_obj_get(root, "uri");
  auto* phase = yyjson_obj_get(root, "phase");
  auto* completed = yyjson_obj_get(root, "completed");
  auto* total = yyjson_obj_get(root, "total");
  auto* cancel = yyjson_obj_get(root, "can_cancel");
  auto* message = yyjson_obj_get(root, "message");
  if (!yyjson_is_uint(session) || !yyjson_is_uint(revision) || !yyjson_is_str(uri) ||
      !yyjson_is_str(phase) || !yyjson_is_uint(completed) || !yyjson_is_uint(total) ||
      yyjson_get_uint(total) > 65536U || yyjson_get_uint(completed) > yyjson_get_uint(total) ||
      !yyjson_is_bool(cancel) || !yyjson_is_str(message)) {
    return result::invalid_argument;
  }
  ipc_scene_progress value;
  value.source = {
      .session = yyjson_get_uint(session),
      .revision = yyjson_get_uint(revision),
      .uri = std::string(yyjson_get_str(uri), yyjson_get_len(uri)),
  };
  bool found{};
  for (std::size_t index = 0; index < phases.size(); ++index) {
    if (phases[index] == std::string_view(yyjson_get_str(phase), yyjson_get_len(phase))) {
      value.phase = static_cast<ipc_scene_phase>(index);
      found = true;
      break;
    }
  }
  value.completed = static_cast<std::uint32_t>(yyjson_get_uint(completed));
  value.total = static_cast<std::uint32_t>(yyjson_get_uint(total));
  value.can_cancel = yyjson_get_bool(cancel);
  value.message.assign(yyjson_get_str(message), yyjson_get_len(message));
  if (auto* budget = yyjson_obj_get(root, "budget")) {
    ipc_scene_budget parsed;
    if (!read_budget(budget, parsed)) {
      return result::invalid_argument;
    }
    value.budget = parsed;
  }
  if (!found || !valid_progress(value) || (response && !scene_phase_terminal(value.phase))) {
    return result::invalid_argument;
  }
  output = std::move(value);
  return result::success;
} catch (...) {
  return result::out_of_memory;
}
} // namespace gneiss
