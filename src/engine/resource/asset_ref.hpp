// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace gneiss::engine::resource {
/** 内部资源强引用。复制共享所有权，最后一个拥有者释放才执行原控制块的删除器。
 * 不保证资源数据线程安全；最后释放发生在当前线程，必须遵循资源所属服务的线程约束。
 * 不允许从裸指针建立所有权；借用 get() 的结果不得超过强引用有效期。 */
template <typename T> class asset_ref final {
public:
  asset_ref() noexcept = default;
  explicit asset_ref(std::shared_ptr<T> value) noexcept : value_(std::move(value)) {}
  asset_ref(const asset_ref&) noexcept = default;
  asset_ref(asset_ref&&) noexcept = default;
  asset_ref& operator=(const asset_ref&) noexcept = default;
  asset_ref& operator=(asset_ref&&) noexcept = default;
  template <typename U>
    requires std::convertible_to<U*, T*>
  asset_ref(const asset_ref<U>& other) noexcept : value_(other.shared()) {}
  template <typename U>
    requires std::convertible_to<U*, T*>
  asset_ref(asset_ref<U>&& other) noexcept : value_(std::move(other.value_)) {}
  [[nodiscard]] T* get() const noexcept { return value_.get(); }
  [[nodiscard]] T* operator->() const noexcept { return get(); }
  template <typename U = T>
    requires(!std::is_void_v<U> && std::same_as<U, T>)
  [[nodiscard]] U& operator*() const noexcept {
    return *value_;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return bool(value_); }
  void reset() noexcept { value_.reset(); }
  [[nodiscard]] bool operator==(const asset_ref&) const noexcept = default;
  /** 仅供缓存回收策略使用；并发环境中不得据此决定对象可写性。 */
  [[nodiscard]] long use_count() const noexcept { return value_.use_count(); }
  /** 标准库互操作边界；不会创建另一份控制块。 */
  [[nodiscard]] const std::shared_ptr<T>& shared() const noexcept { return value_; }
  /** 借用同一拥有者中的对象，保持控制块不变；指针必须由本引用保活。 */
  template <typename U> [[nodiscard]] asset_ref<U> alias(U* pointer) const noexcept {
    return *this && pointer ? asset_ref<U>{std::shared_ptr<U>(value_, pointer)} : asset_ref<U>{};
  }

private:
  template <typename> friend class asset_ref;
  std::shared_ptr<T> value_;
};
}
