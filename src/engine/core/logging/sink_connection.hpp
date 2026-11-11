// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/record.hpp"
#include <memory>
#include <utility>
namespace gneiss::engine::logging {
namespace detail {
struct service_state;
struct sink_slot;
}
class service;
/** 非拥有输出连接，析构不解绑；断开不等待已开始的输出。 */
class GNEISS_API sink_connection {
public:
  sink_connection() = default;
  void disconnect() const;
  bool connected() const;
  void set_level(level) const;

private:
  friend class service;
  sink_connection(std::weak_ptr<detail::service_state> state, std::weak_ptr<detail::sink_slot> slot)
      : state_(std::move(state)), slot_(std::move(slot)) {}
  std::weak_ptr<detail::service_state> state_;
  std::weak_ptr<detail::sink_slot> slot_;
};
class scoped_sink_connection {
public:
  scoped_sink_connection() = default;
  scoped_sink_connection(sink_connection value) : value_(std::move(value)) {}
  scoped_sink_connection(const scoped_sink_connection&) = delete;
  scoped_sink_connection& operator=(const scoped_sink_connection&) = delete;
  scoped_sink_connection(scoped_sink_connection&& other) noexcept
      : value_(std::exchange(other.value_, {})) {}
  scoped_sink_connection& operator=(scoped_sink_connection&& other) {
    if (this != &other) {
      value_.disconnect();
      value_ = std::exchange(other.value_, {});
    }
    return *this;
  }
  ~scoped_sink_connection() { value_.disconnect(); }
  void disconnect() const { value_.disconnect(); }
  bool connected() const { return value_.connected(); }
  void set_level(level value) const { value_.set_level(value); }

private:
  sink_connection value_;
};
}
