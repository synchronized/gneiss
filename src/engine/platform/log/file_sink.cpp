// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "file_sink.hpp"
#include <format>
#include <spdlog/sinks/basic_file_sink.h>

namespace gneiss::engine::logging {
namespace {
class file_sink final : public sink {
public:
  file_sink(const std::filesystem::path& path, bool truncate) : output_(path.native(), truncate) {
    // 原生时间为 steady_clock，不冒充墙钟；只输出原始记录生成的正文。
    output_.set_pattern("%v");
  }
  error write(const event_view& event) override {
    const auto severity = static_cast<std::uint32_t>(event.message.severity);
    if (severity < 1 || severity > 6)
      return error::invalid_argument;
    const auto text = std::format(
        "[time_ns={} sequence={} thread={} context={} level={} source={} category={} result={}] {}",
        event.timestamp_ns, event.sequence, event.thread_id, event.message.context, severity,
        event.message.source, event.message.category, event.message.result.code(),
        event.message.message);
    try {
      spdlog::details::log_msg message{{}, spdlog::level::info, text};
      output_.log(message);
      return error::success;
    } catch (const spdlog::spdlog_ex&) {
      return error::io;
    }
  }
  error flush() override {
    try {
      output_.flush();
      return error::success;
    } catch (const spdlog::spdlog_ex&) {
      return error::io;
    }
  }

private:
  spdlog::sinks::basic_file_sink_st output_;
};
}
result<std::unique_ptr<sink>> create_file_sink(const std::filesystem::path& path, bool truncate) {
  if (path.empty() || path.native().find(std::filesystem::path::value_type{}) !=
                          std::filesystem::path::string_type::npos)
    return error::invalid_argument;
  try {
    return std::unique_ptr<sink>{std::make_unique<file_sink>(path, truncate)};
  } catch (const spdlog::spdlog_ex&) {
    return error::io;
  }
}
} // namespace gneiss::engine::logging
