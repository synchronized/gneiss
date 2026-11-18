// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/logging/export.hpp>

#include <gneiss/engine/core/logging/record.hpp>
#include <gneiss/engine/core/base/string_name.hpp>

#include <format>
#include <memory>

namespace gneiss::logging {
namespace detail {
struct service_state;
}
class service;
/** 可复制的频道句柄；不保活工作线程，关闭后禁用并丢弃提交。 */
class logger {
public:
  logger() = default;
  /** 持有频道名，调用时使用默认服务；允许动态文本，构造可能分配。 */
  GNEISS_LOGGING_API explicit logger(std::string_view channel);
  [[nodiscard]] GNEISS_LOGGING_API bool enabled(level severity) const;
  /** 内部结构化记录；保留上下文/错误码，空来源和类别使用频道。文本借用至返回。 */
  GNEISS_LOGGING_API void write(message_view message) const;
  template <class... Args>
  void write(level severity, std::format_string<Args...> format, Args&&... args) const {
    if (enabled(severity))
      submit(severity, std::format(format, std::forward<Args>(args)...));
  }
  template <class... Args> void trace(std::format_string<Args...> format, Args&&... args) const {
    write(level::trace, format, std::forward<Args>(args)...);
  }
  template <class... Args> void debug(std::format_string<Args...> format, Args&&... args) const {
    write(level::debug, format, std::forward<Args>(args)...);
  }
  template <class... Args> void info(std::format_string<Args...> format, Args&&... args) const {
    write(level::info, format, std::forward<Args>(args)...);
  }
  template <class... Args> void warning(std::format_string<Args...> format, Args&&... args) const {
    write(level::warning, format, std::forward<Args>(args)...);
  }
  template <class... Args> void error(std::format_string<Args...> format, Args&&... args) const {
    write(level::error, format, std::forward<Args>(args)...);
  }
  template <class... Args> void critical(std::format_string<Args...> format, Args&&... args) const {
    write(level::fatal, format, std::forward<Args>(args)...);
  }

private:
  friend class service;
  logger(std::weak_ptr<detail::service_state> state, string_name channel)
      : state_(std::move(state)), channel_(channel) {}
  GNEISS_LOGGING_API std::shared_ptr<detail::service_state> resolve() const;
  GNEISS_LOGGING_API void submit(level severity, std::string_view text) const;
  bool use_default_{};
  std::weak_ptr<detail::service_state> state_;
  string_name channel_;
};
}
