// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/events/signal.hpp>

#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
namespace events = gneiss::events;
void require(bool condition) {
  if (!condition)
    throw std::runtime_error("signal contract test failed");
}
struct receiver {
  int total{};
  void receive(int value) { total += value; }
};
void throws(int) { throw std::runtime_error("listener failure"); }
struct contract_receiver {
  events::signal<void(int)>& source;
  events::connection subscription;
  bool recursion_rejected{};
  bool disconnect_rejected{};
  bool connect_rejected{};
  void receive(int) {
    try {
      source.publish(0);
    } catch (const std::logic_error&) {
      recursion_rejected = true;
    }
    try {
      subscription.disconnect();
    } catch (const std::logic_error&) {
      disconnect_rejected = true;
    }
    try {
      (void)source.sink().connect<&throws>();
    } catch (const std::logic_error&) {
      connect_rejected = true;
    }
  }
};
void test() {
  receiver target;
  events::signal<void(int)> source;
  const auto first = source.sink().connect<&receiver::receive>(target);
  {
    events::scoped_connection second = source.sink().connect<&receiver::receive>(target);
    source.publish(2);
    require(target.total == 4);
    first.disconnect();
    first.disconnect();
    source.publish(3);
    require(target.total == 7 && second.connected() && !first.connected());
    events::scoped_connection moved = std::move(second);
    require(moved.connected() && !second.connected());
  }
  source.publish(9);
  require(target.total == 7);
  // 列表扩容或删除其他订阅不能改变连接身份；旧连接也不能误断新订阅。
  {
    std::vector<events::connection> subscriptions;
    for (int index = 0; index < 64; ++index)
      subscriptions.push_back(source.sink().connect<&receiver::receive>(target));
    const auto removed = subscriptions.front();
    removed.disconnect();
    const auto newest = source.sink().connect<&receiver::receive>(target);
    removed.disconnect();
    require(newest.connected() && subscriptions.back().connected());
    source.publish(1);
    require(target.total == 71);
    for (const auto& subscription : subscriptions)
      subscription.disconnect();
    newest.disconnect();
  }
  events::scoped_connection expired;
  auto old_sink = [&] {
    events::signal<void(int)> temporary;
    expired = temporary.sink().connect<&receiver::receive>(target);
    return temporary.sink();
  }();
  require(!expired.connected());
  expired.disconnect();
  bool rejected{};
  try {
    (void)old_sink.connect<&receiver::receive>(target);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected);
  auto throwing = source.sink().connect<&throws>();
  bool propagated{};
  try {
    source.publish(0);
  } catch (const std::runtime_error&) {
    propagated = true;
  }
  require(propagated);
  throwing.disconnect();
  // 异常离开发布后必须能正常连接、发布和断开。
  contract_receiver contract{source, {}, false, false, false};
  contract.subscription = source.sink().connect<&contract_receiver::receive>(contract);
  source.publish(0);
  require(contract.recursion_rejected && contract.disconnect_rejected && contract.connect_rejected);
  contract.subscription.disconnect();
#ifdef GNEISS_SIGNAL_TEST_THREADS
  bool wrong_thread{};
  std::thread worker([&] {
    try {
      source.publish(0);
    } catch (const std::logic_error&) {
      wrong_thread = true;
    }
  });
  worker.join();
  require(wrong_thread);
#endif
}
}
int main() {
  try {
    test();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
