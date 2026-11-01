// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include <gneiss/app/runtime_log_protocol.hpp>

#include <yyjson.h>

#include <algorithm>
#include <charconv>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

namespace gneiss::host {
namespace {

struct document_deleter final {
  void operator()(yyjson_doc* document) const noexcept { yyjson_doc_free(document); }
};

using document_ptr = std::unique_ptr<yyjson_doc, document_deleter>;

template <typename Integer> void append_integer(std::string& output, Integer value) {
  char buffer[32]{};
  const auto converted = std::to_chars(std::begin(buffer), std::end(buffer), value);
  output.append(buffer, converted.ptr);
}

void append_json_string(std::string& output, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  output.push_back('"');
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    switch (character) {
    case '"':
      output.append("\\\"");
      break;
    case '\\':
      output.append("\\\\");
      break;
    case '\b':
      output.append("\\b");
      break;
    case '\f':
      output.append("\\f");
      break;
    case '\n':
      output.append("\\n");
      break;
    case '\r':
      output.append("\\r");
      break;
    case '\t':
      output.append("\\t");
      break;
    default:
      if (byte < 0x20U) {
        output.append("\\u00");
        output.push_back(hex[byte >> 4U]);
        output.push_back(hex[byte & 0x0FU]);
      } else {
        output.push_back(character);
      }
      break;
    }
  }
  output.push_back('"');
}

[[nodiscard]] bool read_unsigned(yyjson_val* object, const char* key,
                                 std::uint64_t& output) noexcept {
  auto* value = yyjson_obj_get(object, key);
  if (!yyjson_is_uint(value)) {
    return false;
  }
  output = yyjson_get_uint(value);
  return true;
}

[[nodiscard]] bool read_string(yyjson_val* object, const char* key, std::string& output) {
  auto* value = yyjson_obj_get(object, key);
  if (!yyjson_is_str(value)) {
    return false;
  }
  output.assign(yyjson_get_str(value), yyjson_get_len(value));
  return true;
}

} // namespace

runtime_log_line_decoder::runtime_log_line_decoder(std::size_t maximum_line_size) noexcept
    : maximum_line_size_(maximum_line_size) {}

gneiss::engine::error
runtime_log_line_decoder::append(std::string_view bytes,
                                 std::vector<runtime_log_line>& output) noexcept {
  if (maximum_line_size_ == 0U) {
    return gneiss::engine::error::invalid_state;
  }
  try {
    while (!bytes.empty()) {
      const auto newline = bytes.find('\n');
      if (newline == std::string_view::npos) {
        append_segment(bytes);
        break;
      }
      append_segment(bytes.substr(0U, newline));
      const auto operation = emit(output);
      if (operation != gneiss::engine::error::success) {
        return operation;
      }
      bytes.remove_prefix(newline + 1U);
    }
    return gneiss::engine::error::success;
  } catch (...) {
    return gneiss::engine::error::out_of_memory;
  }
}

gneiss::engine::error
runtime_log_line_decoder::finish(std::vector<runtime_log_line>& output) noexcept {
  if (pending_.empty() && !pending_was_truncated_) {
    return gneiss::engine::error::success;
  }
  return emit(output);
}

void runtime_log_line_decoder::reset() noexcept {
  pending_.clear();
  pending_was_truncated_ = false;
}

gneiss::engine::error
runtime_log_line_decoder::emit(std::vector<runtime_log_line>& output) noexcept {
  try {
    output.push_back({pending_, pending_was_truncated_});
    reset();
    return gneiss::engine::error::success;
  } catch (...) {
    return gneiss::engine::error::out_of_memory;
  }
}

void runtime_log_line_decoder::append_segment(std::string_view segment) {
  const auto available = maximum_line_size_ - pending_.size();
  const auto accepted = std::min(available, segment.size());
  pending_.append(segment.data(), accepted);
  pending_was_truncated_ = pending_was_truncated_ || accepted != segment.size();
}

gneiss::engine::error encode_runtime_log_event(const gneiss::engine::event_view& event,
                                               std::string& output) noexcept {
  output.clear();
  try {
    output.append(runtime_log_prefix);
    output.append("{\"version\":1,\"sequence\":");
    append_integer(output, event.sequence);
    output.append(",\"timestamp_ns\":");
    append_integer(output, event.timestamp_ns);
    output.append(",\"severity\":");
    append_integer(output, static_cast<std::uint32_t>(event.message.severity));
    output.append(",\"source\":");
    append_json_string(output, event.message.source);
    output.append(",\"category\":");
    append_json_string(output, event.message.category);
    output.append(",\"thread_id\":");
    append_integer(output, event.thread_id);
    output.append(",\"result\":");
    append_integer(output, event.message.result.code());
    output.append(",\"message\":");
    append_json_string(output, event.message.message);
    output.append("}\n");
    return gneiss::engine::error::success;
  } catch (...) {
    output.clear();
    return gneiss::engine::error::internal;
  }
}

runtime_log_parse_result parse_runtime_log_line(std::string_view line,
                                                runtime_log_record& output) noexcept {
  if (!line.starts_with(runtime_log_prefix)) {
    return runtime_log_parse_result::not_protocol;
  }
  line.remove_prefix(runtime_log_prefix.size());
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1U);
  }
  try {
    const document_ptr document(yyjson_read_opts(const_cast<char*>(line.data()), line.size(),
                                                 YYJSON_READ_NOFLAG, nullptr, nullptr));
    auto* root = document == nullptr ? nullptr : yyjson_doc_get_root(document.get());
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 9U) {
      return runtime_log_parse_result::invalid;
    }
    std::uint64_t version = 0U;
    std::uint64_t severity = 0U;
    auto* operation_value = yyjson_obj_get(root, "result");
    if (!read_unsigned(root, "version", version)) {
      return runtime_log_parse_result::invalid;
    }
    if (version != 1U) {
      return runtime_log_parse_result::unsupported_version;
    }
    runtime_log_record parsed;
    if (!read_unsigned(root, "sequence", parsed.sequence) ||
        !read_unsigned(root, "timestamp_ns", parsed.timestamp_ns) ||
        !read_unsigned(root, "severity", severity) || severity > UINT32_MAX ||
        !read_string(root, "source", parsed.source) ||
        !read_string(root, "category", parsed.category) ||
        !read_unsigned(root, "thread_id", parsed.thread_id) || !yyjson_is_int(operation_value) ||
        !read_string(root, "message", parsed.message)) {
      return runtime_log_parse_result::invalid;
    }
    const auto operation = yyjson_get_sint(operation_value);
    if (operation < std::numeric_limits<std::int32_t>::min() ||
        operation > std::numeric_limits<std::int32_t>::max()) {
      return runtime_log_parse_result::invalid;
    }
    parsed.version = 1U;
    parsed.severity = static_cast<std::uint32_t>(severity);
    parsed.operation = gneiss::engine::error{static_cast<std::int32_t>(operation)};
    output = std::move(parsed);
    return runtime_log_parse_result::success;
  } catch (...) {
    return runtime_log_parse_result::invalid;
  }
}

} // namespace gneiss::host
