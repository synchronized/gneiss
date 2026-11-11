// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/record.hpp"
#include "engine/core/string_name.hpp"
#include <format>
#include <memory>

namespace gneiss::engine::logging {
namespace detail {
struct service_state;
}
class service;
/** 可复制的频道句柄；不保活工作线程，关闭后禁用并丢弃提交。 */
class GNEISS_API logger {
public:
  logger() = default;
  /** 仅借用具有静态生命周期的频道名；动态文本请通过 service.get_logger 创建。 */
  template <std::size_t N>
  explicit logger(const char (&channel)[N]) noexcept
      : default_channel_(channel, N - 1), use_default_(true) {}
  [[nodiscard]] bool enabled(level severity) const;
  /** 内部结构化记录；保留上下文/错误码，空来源和类别使用频道。文本借用至返回。 */
  void write(message_view message) const;
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
  std::shared_ptr<detail::service_state> resolve() const;
  void submit(level severity, std::string_view text) const;
  std::string_view default_channel_;
  bool use_default_{};
  std::weak_ptr<detail::service_state> state_;
  string_name channel_;
};
}
