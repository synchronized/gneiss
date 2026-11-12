// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <entt/signal/delegate.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace gneiss::events {
namespace detail {
// 只检测发布者寿命，不拥有订阅或监听对象；所有访问仍限发布者线程。
struct signal_lifetime {
  void* owner;
  void (*disconnect)(void*, std::uint64_t);
  bool (*connected)(const void*, std::uint64_t);
};
} // namespace detail

template <class Signature> class signal;

/** 非拥有连接；复制共享同一订阅身份，析构不自动断开。源销毁后断开为空操作。 */
class connection {
public:
  connection() = default;
  void disconnect() const {
    if (const auto source = source_.lock())
      source->disconnect(source->owner, id_);
  }
  [[nodiscard]] bool connected() const {
    const auto source = source_.lock();
    return source && source->connected(source->owner, id_);
  }

private:
  template <class> friend class sink;
  connection(std::weak_ptr<detail::signal_lifetime> source, std::uint64_t id)
      : source_(std::move(source)), id_(id) {}
  std::weak_ptr<detail::signal_lifetime> source_;
  std::uint64_t id_{};
};

/** 移动拥有订阅。须在监听者析构前、所属线程且不在发布过程中销毁。 */
class scoped_connection {
public:
  scoped_connection() = default;
  scoped_connection(connection value) : connection_(std::move(value)) {}
  scoped_connection(const scoped_connection&) = delete;
  scoped_connection& operator=(const scoped_connection&) = delete;
  scoped_connection(scoped_connection&& other) noexcept
      : connection_(std::exchange(other.connection_, {})) {}
  scoped_connection& operator=(scoped_connection&& other) {
    if (this != &other) {
      connection_.disconnect();
      connection_ = std::exchange(other.connection_, {});
    }
    return *this;
  }
  ~scoped_connection() { connection_.disconnect(); }
  void disconnect() const { connection_.disconnect(); }
  [[nodiscard]] bool connected() const { return connection_.connected(); }

private:
  connection connection_;
};

/** 仅授予订阅权限；不保活发布者和监听者。过期 Sink 连接抛出契约错误。 */
template <class Signature> class sink {
public:
  template <auto Candidate, class... Instance> connection connect(Instance&... instance) const {
    const auto lifetime = lifetime_.lock();
    if (!lifetime)
      throw std::logic_error("cannot connect to an expired signal");
    auto& source = *static_cast<signal<Signature>*>(lifetime->owner);
    return {lifetime, source.template connect<Candidate>(instance...)};
  }

private:
  template <class> friend class signal;
  explicit sink(std::weak_ptr<detail::signal_lifetime> lifetime) : lifetime_(std::move(lifetime)) {}
  std::weak_ptr<detail::signal_lifetime> lifetime_;
};

/** 所属线程上的同步通知；不保证顺序，参数仅在调用期间有效。回调异常原样传播。 */
template <class... Args> class signal<void(Args...)> {
public:
  signal() = default;
  signal(const signal&) = delete;
  signal& operator=(const signal&) = delete;
  ~signal() { check_idle(); }
  [[nodiscard]] events::sink<void(Args...)> sink() const {
    check_thread();
    return events::sink<void(Args...)>{lifetime_};
  }
  void publish(Args... args) {
    check_idle();
    struct publish_guard {
      bool& active;
      ~publish_guard() { active = false; }
    } guard{publishing_};
    publishing_ = true;
    for (const auto& entry : subscriptions_)
      entry.callback(args...);
  }

private:
  template <class> friend class sink;
  struct subscription {
    std::uint64_t id;
    entt::delegate<void(Args...)> callback;
  };
  void check_thread() const {
    if (owner_ != std::this_thread::get_id())
      throw std::logic_error("signal must be accessed on its owner thread");
  }
  void check_idle() const {
    check_thread();
    if (publishing_)
      throw std::logic_error("signal cannot be modified or recursively published during publish");
  }
  void disconnect(std::uint64_t id) {
    check_idle();
    std::erase_if(subscriptions_, [id](const auto& entry) { return entry.id == id; });
  }
  bool connected(std::uint64_t id) const {
    check_thread();
    return std::ranges::any_of(subscriptions_, [id](const auto& entry) { return entry.id == id; });
  }
  template <auto Candidate, class... Instance> std::uint64_t connect(Instance&... instance) {
    check_idle();
    if (next_id_ == 0)
      throw std::overflow_error("signal connection identity exhausted");
    entt::delegate<void(Args...)> callback;
    callback.template connect<Candidate>(instance...);
    const auto id = next_id_++;
    subscriptions_.push_back({id, callback});
    return id;
  }

  std::thread::id owner_{std::this_thread::get_id()};
  bool publishing_{};
  std::uint64_t next_id_{1};
  std::vector<subscription> subscriptions_;
  std::shared_ptr<detail::signal_lifetime> lifetime_ =
      std::make_shared<detail::signal_lifetime>(detail::signal_lifetime{
          this, [](void* owner, std::uint64_t id) { static_cast<signal*>(owner)->disconnect(id); },
          [](const void* owner, std::uint64_t id) {
            return static_cast<const signal*>(owner)->connected(id);
          }});
};
} // namespace gneiss::events
