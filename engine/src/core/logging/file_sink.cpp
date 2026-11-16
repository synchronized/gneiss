// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "file_sink.hpp"
#include "console_sink.hpp"
#include <format>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace gneiss::logging {
namespace {
class text_sink final : public sink {
public:
  explicit text_sink(std::unique_ptr<spdlog::sinks::sink> output) : output_(std::move(output)) {
    // 原生时间为 steady_clock，不冒充墙钟；只输出原始记录生成的正文。
    output_->set_pattern("%v");
  }
  error write(const record_view& event) override {
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
      output_->log(message);
      return error::success;
    } catch (const spdlog::spdlog_ex&) {
      return error::io;
    }
  }
  error flush() override {
    try {
      output_->flush();
      return error::success;
    } catch (const spdlog::spdlog_ex&) {
      return error::io;
    }
  }

private:
  std::unique_ptr<spdlog::sinks::sink> output_;
};
}
result<std::unique_ptr<sink>> create_file_sink(const std::filesystem::path& path, bool truncate) {
  if (path.empty() || path.native().find(std::filesystem::path::value_type{}) !=
                          std::filesystem::path::string_type::npos)
    return error::invalid_argument;
  try {
    return std::unique_ptr<sink>{std::make_unique<text_sink>(
        std::make_unique<spdlog::sinks::basic_file_sink_st>(path.native(), truncate))};
  } catch (const spdlog::spdlog_ex&) {
    return error::io;
  }
}
result<std::unique_ptr<sink>> create_rotating_file_sink(const std::filesystem::path& path,
                                                        std::size_t maximum_size,
                                                        std::size_t backup_count) {
  if (path.empty() || maximum_size == 0 || backup_count == 0 ||
      path.native().find(std::filesystem::path::value_type{}) !=
          std::filesystem::path::string_type::npos)
    return error::invalid_argument;
  try {
    return std::unique_ptr<sink>{
        std::make_unique<text_sink>(std::make_unique<spdlog::sinks::rotating_file_sink_st>(
            path.native(), maximum_size, backup_count))};
  } catch (const spdlog::spdlog_ex&) {
    return error::io;
  }
}
std::unique_ptr<sink> create_console_sink() {
  return std::make_unique<text_sink>(std::make_unique<spdlog::sinks::stderr_sink_st>());
}
} // namespace gneiss::logging
