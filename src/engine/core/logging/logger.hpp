// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/record.hpp"
#include "engine/core/string_name.hpp"
#include <format>
#include <memory>

namespace gneiss::engine::logging {
namespace detail {
struct runtime_state;
}
class runtime;
/** 可复制的频道句柄；不保活工作线程，关闭后禁用并丢弃提交。 */
class GNEISS_API logger {
public:
  logger() = default;
  [[nodiscard]] bool enabled(level severity) const noexcept;
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
  friend class runtime;
  logger(std::weak_ptr<detail::runtime_state> state, string_name channel)
      : state_(std::move(state)), channel_(channel) {}
  void submit(level severity, std::string_view text) const;
  std::weak_ptr<detail::runtime_state> state_;
  string_name channel_;
};
}
