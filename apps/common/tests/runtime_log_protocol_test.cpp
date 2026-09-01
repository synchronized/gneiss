// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/app/runtime_log_protocol.h>

#include <string>

int main() {
  constexpr std::string_view source = "game_module";
  constexpr std::string_view category = "测试";
  constexpr std::string_view message = "第一行\n\"第二行\"\\尾部";
  const gneiss_log_event event = {
      .struct_size = sizeof(gneiss_log_event),
      .severity = GNEISS_LOG_WARNING,
      .sequence = 7U,
      .timestamp_ns = 42U,
      .thread_id = 3U,
      .source = source.data(),
      .source_length = source.size(),
      .category = category.data(),
      .category_length = category.size(),
      .message = message.data(),
      .message_length = message.size(),
      .result = GNEISS_ERROR_NOT_READY,
      .flags = 0U,
      .reserved = {},
  };

  std::string encoded;
  if (gneiss::app::encode_runtime_log_event(event, encoded) != gneiss::result::success ||
      !encoded.starts_with(gneiss::app::runtime_log_prefix) ||
      encoded.find('\n') != encoded.size() - 1U) {
    return 1;
  }
  encoded.pop_back();
  gneiss::app::runtime_log_record parsed;
  if (gneiss::app::parse_runtime_log_line(encoded, parsed) !=
          gneiss::app::runtime_log_parse_result::success ||
      parsed.severity != event.severity || parsed.sequence != event.sequence ||
      parsed.timestamp_ns != event.timestamp_ns || parsed.thread_id != event.thread_id ||
      parsed.source != source || parsed.category != category || parsed.message != message ||
      parsed.operation != event.result) {
    return 2;
  }
  if (gneiss::app::parse_runtime_log_line("第三方普通输出", parsed) !=
      gneiss::app::runtime_log_parse_result::not_protocol) {
    return 3;
  }
  auto unsupported = encoded;
  unsupported.replace(unsupported.find("\"version\":1"), 11U, "\"version\":2");
  if (gneiss::app::parse_runtime_log_line(unsupported, parsed) !=
      gneiss::app::runtime_log_parse_result::unsupported_version) {
    return 4;
  }
  return gneiss::app::parse_runtime_log_line("@gneiss-log-v1 {broken", parsed) ==
                 gneiss::app::runtime_log_parse_result::invalid
             ? 0
             : 5;
}
