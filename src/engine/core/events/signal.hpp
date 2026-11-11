// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <entt/signal/sigh.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace gneiss::events {
namespace detail {
struct connection_source {
  virtual ~connection_source() = default;
  virtual void disconnect(std::uint64_t id) = 0;
  virtual bool connected(std::uint64_t id) const = 0;
};

template <class Signature> struct signal_state;

template <class... Args> struct signal_state<void(Args...)> final : connection_source {
  struct subscription {
    entt::delegate<void(Args...)> callback;
    entt::connection connection;
    void invoke(Args... args) { callback(args...); }
  };

  std::thread::id owner{std::this_thread::get_id()};
  bool publishing{};
  std::uint64_t next_id{1};
  entt::sigh<void(Args...)> signal;
  std::unordered_map<std::uint64_t, std::unique_ptr<subscription>> subscriptions;

  void check_thread() const {
    if (owner != std::this_thread::get_id())
      throw std::logic_error("signal must be accessed on its owner thread");
  }
  void check_idle() const {
    check_thread();
    if (publishing)
      throw std::logic_error("signal cannot be modified or recursively published during publish");
  }
  void disconnect(std::uint64_t id) override {
    check_idle();
    const auto found = subscriptions.find(id);
    if (found != subscriptions.end()) {
      found->second->connection.release();
      subscriptions.erase(found);
    }
  }
  bool connected(std::uint64_t id) const override {
    check_thread();
    return subscriptions.contains(id);
  }
  template <auto Candidate, class... Instance> std::uint64_t connect(Instance&... instance) {
    check_idle();
    if (next_id == 0)
      throw std::overflow_error("signal connection identity exhausted");
    auto entry = std::make_unique<subscription>();
    entry->callback.template connect<Candidate>(instance...);
    const auto id = next_id++;
    auto [position, inserted] = subscriptions.emplace(id, std::move(entry));
    (void)inserted;
    // 每次订阅使用独立地址，避免 EnTT 的目标去重使旧连接断开新订阅。
    try {
      position->second->connection =
          entt::sink{signal}.template connect<&subscription::invoke>(*position->second);
    } catch (...) {
      subscriptions.erase(position);
      throw;
    }
    return id;
  }
};
} // namespace detail

/** 非拥有连接；复制共享同一订阅身份，析构不自动断开。源销毁后断开为空操作。 */
class connection {
public:
  connection() = default;
  void disconnect() const {
    if (const auto source = source_.lock())
      source->disconnect(id_);
  }
  [[nodiscard]] bool connected() const {
    const auto source = source_.lock();
    return source && source->connected(id_);
  }

private:
  template <class> friend class sink;
  connection(std::weak_ptr<detail::connection_source> source, std::uint64_t id)
      : source_(std::move(source)), id_(id) {}
  std::weak_ptr<detail::connection_source> source_;
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
    const auto state = state_.lock();
    if (!state)
      throw std::logic_error("cannot connect to an expired signal");
    return {state, state->template connect<Candidate>(instance...)};
  }

private:
  template <class> friend class signal;
  explicit sink(std::weak_ptr<detail::signal_state<Signature>> state) : state_(std::move(state)) {}
  std::weak_ptr<detail::signal_state<Signature>> state_;
};

template <class Signature> class signal;

/** 所属线程上的同步通知；不保证顺序，参数仅在调用期间有效。回调异常原样传播。 */
template <class... Args> class signal<void(Args...)> {
public:
  signal() = default;
  signal(const signal&) = delete;
  signal& operator=(const signal&) = delete;
  ~signal() { state_->check_idle(); }
  [[nodiscard]] events::sink<void(Args...)> sink() const {
    state_->check_thread();
    return events::sink<void(Args...)>{state_};
  }
  void publish(Args... args) {
    state_->check_idle();
    struct publish_guard {
      bool& active;
      ~publish_guard() { active = false; }
    } guard{state_->publishing};
    state_->publishing = true;
    state_->signal.publish(args...);
  }

private:
  std::shared_ptr<detail::signal_state<void(Args...)>> state_ =
      std::make_shared<detail::signal_state<void(Args...)>>();
};
} // namespace gneiss::events
