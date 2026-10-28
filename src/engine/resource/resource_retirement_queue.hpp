// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace gneiss::engine {

/** 单所属线程消费的资源销毁队列。创建时分配节点，最后释放时只入队。
 * close 必须在所属线程、服务仍有效时调用；关闭后删除器仅清理无服务依赖的对象。
 * Finalize 在析构前接收服务上下文，关闭后为 nullptr，且必须 noexcept。 */
class resource_retirement_queue final {
  struct node {
    node* next{};
    void (*destroy)(node*, void*) noexcept {};
  };
  struct endpoint {
    std::mutex mutex;
    node* head{};
    bool detached{};
    void retire(node* value) noexcept {
      {
        std::lock_guard lock(mutex);
        if (!detached) {
          value->next = head;
          head = value;
          return;
        }
      }
      value->destroy(value, nullptr);
    }
  };
  template <typename T, typename Finalize> struct allocation final : node {
    template <typename Factory>
    allocation(Finalize finalizer, Factory&& factory)
        : finalize(std::move(finalizer)), value(factory()) {
      this->destroy = [](node* entry, void* owner) noexcept {
        auto* self = static_cast<allocation*>(entry);
        self->finalize(self->value, owner);
        delete self;
      };
    }
    Finalize finalize;
    T value;
  };

public:
  explicit resource_retirement_queue(void* owner) : context_(owner) {}
  resource_retirement_queue(const resource_retirement_queue&) = delete;
  resource_retirement_queue& operator=(const resource_retirement_queue&) = delete;
  ~resource_retirement_queue() { close(); }

  template <typename T, typename Finalize, typename... Args>
  std::shared_ptr<T> make(Finalize finalize, Args&&... args) {
    return make_with_factory<T>(std::move(finalize),
                                [&]() { return T(std::forward<Args>(args)...); });
  }

  /** 工厂在创建节点时同步构造对象；C++20 保证消除拷贝，无需开放私有构造或增加控制块。 */
  template <typename T, typename Finalize, typename Factory>
  std::shared_ptr<T> make_with_factory(Finalize finalize, Factory&& factory) {
    static_assert(std::is_same_v<std::invoke_result_t<Factory&>, T>);
    static_assert(std::is_nothrow_invocable_v<Finalize&, T&, void*>);
    static_assert(std::is_nothrow_destructible_v<T>);
    check_owner();
    // 创建只发生在所属线程；生产者只释放引用，不创建节点。
    if (closed_)
      std::terminate();
    auto* entry = new allocation<T, Finalize>(std::move(finalize), std::forward<Factory>(factory));
    return std::shared_ptr<T>(&entry->value,
                              [state = state_, entry](T*) noexcept { state->retire(entry); });
  }

  /** 排空包括析构依赖产生的后续节点；不在锁内执行对象析构。 */
  std::size_t drain() noexcept {
    check_owner();
    std::size_t count{};
    for (;;) {
      node* batch{};
      {
        std::lock_guard lock(state_->mutex);
        batch = std::exchange(state_->head, nullptr);
      }
      if (!batch)
        return count;
      while (batch) {
        auto* current = batch;
        batch = batch->next;
        current->destroy(current, context_);
        ++count;
      }
    }
  }

  /** 排空和 detached 转换使用同一把锁，关闭竞争不会遗留无人消费的节点。 */
  void close() noexcept {
    check_owner();
    if (closed_)
      return;
    for (;;) {
      (void)drain();
      std::lock_guard lock(state_->mutex);
      if (!state_->head) {
        state_->detached = true;
        closed_ = true;
        context_ = nullptr;
        return;
      }
    }
  }

private:
  void check_owner() const noexcept {
    if (std::this_thread::get_id() != owner_)
      std::terminate();
  }
  std::shared_ptr<endpoint> state_{std::make_shared<endpoint>()};
  void* context_{};
  bool closed_{};
  const std::thread::id owner_{std::this_thread::get_id()};
};

} // namespace gneiss::engine
