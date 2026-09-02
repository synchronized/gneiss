// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "ipc_protocol.h"

#include <yyjson.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace {

constexpr std::size_t max_token_size = 256U;
constexpr std::size_t max_capability_count = 32U;
constexpr std::size_t max_capability_size = 64U;
constexpr std::size_t max_text_size = 16U * 1024U;

using document_ptr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using mutable_document_ptr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

bool valid_type(gneiss::ipc_message_type type) noexcept {
  return type >= gneiss::ipc_message_type::hello &&
         type <= gneiss::ipc_message_type::inspection_snapshot;
}

bool valid_capabilities(std::span<const std::string> capabilities) noexcept {
  return capabilities.size() <= max_capability_count &&
         std::ranges::all_of(capabilities, [](const auto& capability) {
           return !capability.empty() && capability.size() <= max_capability_size;
         });
}

bool add_capabilities(yyjson_mut_doc* document, yyjson_mut_val* root,
                      std::span<const std::string> capabilities) noexcept {
  auto* array = yyjson_mut_arr(document);
  if (array == nullptr) {
    return false;
  }
  for (const auto& capability : capabilities) {
    if (!yyjson_mut_arr_add_strncpy(document, array, capability.data(), capability.size())) {
      return false;
    }
  }
  return yyjson_mut_obj_add_val(document, root, "capabilities", array);
}

bool parse_capabilities(yyjson_val* root, std::vector<std::string>& output) {
  auto* array = yyjson_obj_get(root, "capabilities");
  if (!yyjson_is_arr(array) || yyjson_arr_size(array) > max_capability_count) {
    return false;
  }
  std::vector<std::string> parsed;
  parsed.reserve(yyjson_arr_size(array));
  std::size_t index = 0U;
  std::size_t count = 0U;
  yyjson_val* value = nullptr;
  yyjson_arr_foreach(array, index, count, value) {
    if (!yyjson_is_str(value)) {
      return false;
    }
    const auto length = yyjson_get_len(value);
    if (length == 0U || length > max_capability_size) {
      return false;
    }
    parsed.emplace_back(yyjson_get_str(value), length);
  }
  output = std::move(parsed);
  return true;
}

const char* state_name(gneiss::ipc_runtime_state state) noexcept {
  switch (state) {
  case gneiss::ipc_runtime_state::loading:
    return "loading";
  case gneiss::ipc_runtime_state::ready:
    return "ready";
  case gneiss::ipc_runtime_state::running:
    return "running";
  case gneiss::ipc_runtime_state::paused:
    return "paused";
  case gneiss::ipc_runtime_state::stopping:
    return "stopping";
  }
  return nullptr;
}

bool parse_state(yyjson_val* root, gneiss::ipc_runtime_state& output) noexcept {
  auto* value = yyjson_obj_get(root, "state");
  if (!yyjson_is_str(value)) {
    return false;
  }
  const std::string_view name(yyjson_get_str(value), yyjson_get_len(value));
  if (name == "loading") {
    output = gneiss::ipc_runtime_state::loading;
  } else if (name == "ready") {
    output = gneiss::ipc_runtime_state::ready;
  } else if (name == "running") {
    output = gneiss::ipc_runtime_state::running;
  } else if (name == "paused") {
    output = gneiss::ipc_runtime_state::paused;
  } else if (name == "stopping") {
    output = gneiss::ipc_runtime_state::stopping;
  } else {
    return false;
  }
  return true;
}

bool constant_time_equal(std::string_view left, std::string_view right) noexcept {
  std::size_t difference = left.size() ^ right.size();
  const auto count = (std::max)(left.size(), right.size());
  for (std::size_t index = 0U; index < count; ++index) {
    const auto left_value = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
    const auto right_value = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
    difference |= left_value ^ right_value;
  }
  return difference == 0U;
}

} // namespace

namespace gneiss {

result encode_ipc_message(const ipc_message& message, ipc_frame& output) noexcept {
  if (!valid_type(message.type) || !valid_capabilities(message.capabilities) ||
      message.session_token.size() > max_token_size || message.text.size() > max_text_size) {
    return result::invalid_argument;
  }
  try {
    mutable_document_ptr document(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    if (!document) {
      return result::out_of_memory;
    }
    auto* root = yyjson_mut_obj(document.get());
    if (root == nullptr) {
      return result::out_of_memory;
    }
    bool valid = true;
    switch (message.type) {
    case ipc_message_type::hello:
      valid =
          !message.session_token.empty() &&
          yyjson_mut_obj_add_strncpy(document.get(), root, "token", message.session_token.data(),
                                     message.session_token.size()) &&
          add_capabilities(document.get(), root, message.capabilities);
      break;
    case ipc_message_type::hello_ack:
      valid = add_capabilities(document.get(), root, message.capabilities);
      break;
    case ipc_message_type::state_changed: {
      const auto* name = state_name(message.runtime_state);
      valid = name != nullptr && yyjson_mut_obj_add_str(document.get(), root, "state", name);
      break;
    }
    case ipc_message_type::log_event:
      valid = yyjson_mut_obj_add_strncpy(document.get(), root, "event", message.text.data(),
                                         message.text.size());
      break;
    case ipc_message_type::error:
      valid = yyjson_mut_obj_add_sint(document.get(), root, "code", message.code) &&
              yyjson_mut_obj_add_strncpy(document.get(), root, "message", message.text.data(),
                                         message.text.size());
      break;
    case ipc_message_type::ping:
    case ipc_message_type::pong:
      valid = yyjson_mut_obj_add_uint(document.get(), root, "nonce", message.nonce);
      break;
    case ipc_message_type::shutdown_complete:
      valid = yyjson_mut_obj_add_sint(document.get(), root, "exit_code", message.code);
      break;
    case ipc_message_type::inspection_snapshot:
      return result::unsupported;
    case ipc_message_type::ready:
    case ipc_message_type::pause:
    case ipc_message_type::resume:
    case ipc_message_type::stop:
      break;
    }
    if (!valid) {
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
    ipc_frame encoded;
    encoded.protocol_major = ipc_protocol_major;
    encoded.protocol_minor = ipc_protocol_minor;
    encoded.message_type = static_cast<std::uint16_t>(message.type);
    encoded.payload.assign(reinterpret_cast<const std::uint8_t*>(json.get()),
                           reinterpret_cast<const std::uint8_t*>(json.get()) + length);
    output = std::move(encoded);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result decode_ipc_message(const ipc_frame& frame, ipc_message& output) noexcept {
  if (frame.protocol_major != ipc_protocol_major ||
      frame.payload.size() > ipc_protocol_max_json_size) {
    return result::unsupported;
  }
  const auto type = static_cast<ipc_message_type>(frame.message_type);
  if (!valid_type(type)) {
    return result::unsupported;
  }
  try {
    document_ptr document(yyjson_read(reinterpret_cast<const char*>(frame.payload.data()),
                                      frame.payload.size(), YYJSON_READ_NOFLAG),
                          &yyjson_doc_free);
    auto* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!yyjson_is_obj(root)) {
      return result::invalid_argument;
    }

    ipc_message decoded;
    decoded.type = type;
    bool valid = true;
    switch (type) {
    case ipc_message_type::hello: {
      auto* token = yyjson_obj_get(root, "token");
      valid = yyjson_is_str(token) && yyjson_get_len(token) != 0U &&
              yyjson_get_len(token) <= max_token_size;
      if (valid) {
        decoded.session_token.assign(yyjson_get_str(token), yyjson_get_len(token));
        valid = parse_capabilities(root, decoded.capabilities);
      }
      break;
    }
    case ipc_message_type::hello_ack:
      valid = parse_capabilities(root, decoded.capabilities);
      break;
    case ipc_message_type::state_changed:
      valid = parse_state(root, decoded.runtime_state);
      break;
    case ipc_message_type::log_event: {
      auto* event = yyjson_obj_get(root, "event");
      valid = yyjson_is_str(event) && yyjson_get_len(event) <= max_text_size;
      if (valid) {
        decoded.text.assign(yyjson_get_str(event), yyjson_get_len(event));
      }
      break;
    }
    case ipc_message_type::error: {
      auto* code = yyjson_obj_get(root, "code");
      auto* message = yyjson_obj_get(root, "message");
      valid = yyjson_is_int(code) &&
              yyjson_get_sint(code) >= std::numeric_limits<std::int32_t>::min() &&
              yyjson_get_sint(code) <= std::numeric_limits<std::int32_t>::max() &&
              yyjson_is_str(message) && yyjson_get_len(message) <= max_text_size;
      if (valid) {
        decoded.code = static_cast<std::int32_t>(yyjson_get_sint(code));
        decoded.text.assign(yyjson_get_str(message), yyjson_get_len(message));
      }
      break;
    }
    case ipc_message_type::ping:
    case ipc_message_type::pong: {
      auto* nonce = yyjson_obj_get(root, "nonce");
      valid = yyjson_is_uint(nonce);
      if (valid) {
        decoded.nonce = yyjson_get_uint(nonce);
      }
      break;
    }
    case ipc_message_type::shutdown_complete: {
      auto* exit_code = yyjson_obj_get(root, "exit_code");
      valid = yyjson_is_int(exit_code) &&
              yyjson_get_sint(exit_code) >= std::numeric_limits<std::int32_t>::min() &&
              yyjson_get_sint(exit_code) <= std::numeric_limits<std::int32_t>::max();
      if (valid) {
        decoded.code = static_cast<std::int32_t>(yyjson_get_sint(exit_code));
      }
      break;
    }
    case ipc_message_type::inspection_snapshot:
      return result::unsupported;
    case ipc_message_type::ready:
    case ipc_message_type::pause:
    case ipc_message_type::resume:
    case ipc_message_type::stop:
      break;
    }
    if (!valid) {
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

result make_ipc_hello(std::string_view session_token, std::span<const std::string> capabilities,
                      ipc_frame& output) noexcept {
  try {
    ipc_message message;
    message.type = ipc_message_type::hello;
    message.session_token.assign(session_token);
    message.capabilities.assign(capabilities.begin(), capabilities.end());
    return encode_ipc_message(message, output);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result accept_ipc_hello(const ipc_frame& hello, std::string_view expected_token,
                        std::span<const std::string> supported_capabilities,
                        ipc_frame& acknowledgment,
                        std::vector<std::string>& negotiated_capabilities) noexcept {
  if (!valid_capabilities(supported_capabilities)) {
    return result::invalid_argument;
  }
  ipc_message message;
  auto operation = decode_ipc_message(hello, message);
  if (operation != result::success) {
    return operation;
  }
  if (message.type != ipc_message_type::hello ||
      !constant_time_equal(message.session_token, expected_token)) {
    return result::invalid_argument;
  }
  try {
    std::vector<std::string> negotiated;
    for (const auto& requested : message.capabilities) {
      if (std::ranges::find(supported_capabilities, requested) != supported_capabilities.end() &&
          std::ranges::find(negotiated, requested) == negotiated.end()) {
        negotiated.push_back(requested);
      }
    }
    ipc_message response;
    response.type = ipc_message_type::hello_ack;
    response.capabilities = negotiated;
    operation = encode_ipc_message(response, acknowledgment);
    if (operation == result::success) {
      acknowledgment.protocol_minor = (std::min)(hello.protocol_minor, ipc_protocol_minor);
      negotiated_capabilities = std::move(negotiated);
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result accept_ipc_hello_ack(const ipc_frame& acknowledgment,
                            std::span<const std::string> requested_capabilities,
                            std::vector<std::string>& negotiated_capabilities) noexcept {
  if (!valid_capabilities(requested_capabilities)) {
    return result::invalid_argument;
  }
  if (acknowledgment.protocol_minor > ipc_protocol_minor) {
    return result::unsupported;
  }
  ipc_message message;
  const auto operation = decode_ipc_message(acknowledgment, message);
  if (operation != result::success) {
    return operation;
  }
  std::vector<std::string> unique;
  if (message.type != ipc_message_type::hello_ack) {
    return result::invalid_argument;
  }
  try {
    for (const auto& capability : message.capabilities) {
      if (std::ranges::find(requested_capabilities, capability) == requested_capabilities.end() ||
          std::ranges::find(unique, capability) != unique.end()) {
        return result::invalid_argument;
      }
      unique.push_back(capability);
    }
    negotiated_capabilities = std::move(message.capabilities);
    return result::success;
  } catch (...) {
    return result::internal;
  }
}

ipc_timeout_tracker::ipc_timeout_tracker(clock::duration timeout) noexcept
    : timeout_(timeout > clock::duration::zero() ? timeout : clock::duration::zero()) {}

void ipc_timeout_tracker::reset(clock::time_point now) noexcept { last_observed_ = now; }

bool ipc_timeout_tracker::expired(clock::time_point now) const noexcept {
  return timeout_ == clock::duration::zero() || now - last_observed_ >= timeout_;
}

result ipc_inspection_sequence_tracker::begin(std::uint64_t session_id,
                                              std::uint64_t first_sequence) noexcept {
  if (session_id == 0U || first_sequence == 0U) {
    return result::invalid_argument;
  }
  session_id_ = session_id;
  next_sequence_ = first_sequence;
  return result::success;
}

void ipc_inspection_sequence_tracker::reset() noexcept {
  session_id_ = 0U;
  next_sequence_ = 0U;
}

ipc_inspection_sequence_result
ipc_inspection_sequence_tracker::observe(ipc_inspection_stamp stamp) noexcept {
  if (session_id_ == 0U || stamp.session_id == 0U || stamp.sequence == 0U || next_sequence_ == 0U) {
    return ipc_inspection_sequence_result::invalid;
  }
  if (stamp.session_id != session_id_) {
    return ipc_inspection_sequence_result::stale_session;
  }
  if (stamp.sequence < next_sequence_) {
    return ipc_inspection_sequence_result::duplicate;
  }
  if (stamp.sequence > next_sequence_) {
    return ipc_inspection_sequence_result::gap;
  }
  if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    reset();
  } else {
    ++next_sequence_;
  }
  return ipc_inspection_sequence_result::accepted;
}

} // namespace gneiss
