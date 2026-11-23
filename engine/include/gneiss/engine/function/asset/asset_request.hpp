// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "asset_loading.hpp"
#include <gneiss/engine/core/base/result.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/resource/asset_diagnostic.hpp>
#include <gneiss/engine/resource/asset_ref.hpp>
#include "render_resource.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace gneiss {
namespace detail {
struct asset_request_access;
template <class T>
  requires requires(const T& value) { value.snapshot(); }
auto capture_asset_content(const T& value) {
  if constexpr (requires { value.content(); })
    return value.content();
  else
    return value.snapshot();
}
// 移植 Granit 示例 asset_state：请求共享结果，观察者各自拥有取消权。
struct asset_state {
  std::mutex mutex;
  std::function<void()> changed;
  std::vector<std::function<void()>> completed;
  render_resource_ref target;
  std::shared_ptr<const asset> identity;
  const void* native_address{};
  std::shared_ptr<const void> snapshot;
  error status{error::not_ready};
  std::shared_ptr<const void> value;
  serialization::diagnostic diagnostic;
  std::size_t source_bytes{};
  asset_load_metrics metrics;
};

}
/** 统一资产请求；复制共享工作，取消仅放弃本观察者，成功领取一次只读结果。
 * 不驱动队列、不等待 I/O；结果可晚于 manager 销毁。请求对象限所属线程操作。 */
template <typename T> class asset_request final {
public:
  using value_type = T;
  using reference = std::conditional_t<std::is_same_v<T, render_resource_ref>, render_resource_ref,
                                       asset_ref<const T>>;
  asset_request() noexcept = default;
  explicit asset_request(std::shared_ptr<detail::asset_state> state) noexcept
      : state_(std::move(state)) {}
  asset_request(const asset_request&) = default;
  asset_request& operator=(const asset_request& other) {
    if (this != &other) {
      cancel();
      state_ = other.state_;
      source_bytes_ = other.source_bytes_;
      metrics_ = other.metrics_;
    }
    return *this;
  }
  asset_request(asset_request&&) noexcept = default;
  asset_request& operator=(asset_request&& other) noexcept {
    if (this != &other) {
      cancel();
      state_ = std::move(other.state_);
      source_bytes_ = other.source_bytes_;
      metrics_ = std::move(other.metrics_);
    }
    return *this;
  }
  ~asset_request() { cancel(); }
  [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
  template <class U>
    requires((std::is_same_v<T, render_resource_ref> && render_resource<U>) ||
             (render_resource<T> && std::is_same_v<U, render_resource_ref>))
  asset_request(const asset_request<U>& other) noexcept : state_(other.state_) {}
  [[nodiscard]] error status() const noexcept {
    if (!state_)
      return error::invalid_state;
    const std::scoped_lock lock(state_->mutex);
    return state_->status;
  }
  [[nodiscard]] asset_diagnostic diagnostic() const {
    if (!state_)
      return {error::invalid_state, 0, {}, {}};
    const std::scoped_lock lock(state_->mutex);
    return {state_->status, state_->diagnostic.byte_offset, state_->diagnostic.path,
            state_->diagnostic.message};
  }
  /** 已预留的运行资源身份；不表示内容或 GPU 已就绪。 */
  [[nodiscard]] auto resource() const {
    if constexpr (std::is_same_v<T, asset>)
      return state_ ? (state_->target ? state_->target.base() : ref<const asset>{state_->identity})
                    : ref<const asset>{};
    else if constexpr (std::is_same_v<T, render_resource_ref>)
      return state_ ? state_->target : render_resource_ref{};
    else if constexpr (render_resource<T>)
      return state_ ? state_->target.template as<T>() : ref<const T>{};
    else if constexpr (std::derived_from<T, asset>)
      return state_ && state_->identity ? asset_cast<T>(ref<const asset>{state_->identity})
                                        : ref<const T>{};
    else
      return ref<const T>{};
  }
  /** 按注册的实际 C++ 类型借用同一身份，供序列化引用绑定；不复制对象。 */
  [[nodiscard]] ref<void> native_reference() const
    requires std::is_same_v<T, asset>
  {
    auto identity = resource();
    return identity && state_->native_address
               ? identity.alias(const_cast<void*>(state_->native_address))
               : ref<void>{};
  }
  template <render_resource U> [[nodiscard]] asset_ref<const U> get() const {
    return state_ ? state_->target.template as<U>() : ref<const U>{};
  }
  /** 本次请求完成时的固定 CPU 内容，与身份的当前内容分离；需要保留时须在 take/cancel 前取得。 */
  template <class U = T>
    requires(std::same_as<U, T> &&
             requires(const U& value) { detail::capture_asset_content(value); })
  [[nodiscard]] auto snapshot() const {
    using Ref = decltype(detail::capture_asset_content(std::declval<const U&>()));
    using Content = typename Ref::element_type;
    return Ref{std::static_pointer_cast<Content>(state_ ? state_->snapshot
                                                        : std::shared_ptr<const void>{})};
  }
  void cancel() noexcept {
    auto released = std::move(state_);
    if (released && released->changed)
      released->changed();
  }
  /** 最近一次成功领取的读取与解码统计；取消不清除已领取统计。 */
  const asset_load_metrics& metrics() const noexcept { return state_ ? state_->metrics : metrics_; }
  /** 最近一次成功领取的源文件字节数；尚未成功领取时为零，成功后可在请求失效时查询。 */
  [[nodiscard]] std::size_t source_bytes() const noexcept { return source_bytes_; }
  /** 非阻塞领取；not_ready 保留请求，成功后请求失效。失败携带解析诊断。 */
  [[nodiscard]] result<reference, asset_diagnostic> take() {
    if (!state_)
      return asset_diagnostic{error::invalid_state, 0, {}, {}};
    auto state = state_;
    const std::scoped_lock lock(state->mutex);
    if (state->status != error::success)
      return asset_diagnostic{state->status, state->diagnostic.byte_offset, state->diagnostic.path,
                              state->diagnostic.message};
    if (!state->value && !state->target)
      return asset_diagnostic{error::internal, 0, {}, {}};
    reference value;
    if constexpr (std::is_same_v<T, render_resource_ref>) {
      value = state->target;
    } else if constexpr (std::is_same_v<T, asset>) {
      value = resource();
    } else {
      value = state->target ? state->target.as<T>()
                            : reference{std::static_pointer_cast<const T>(state->value)};
    }
    if (!value)
      return asset_diagnostic{error::invalid_argument, 0, {}, {}};
    source_bytes_ = state->source_bytes;
    metrics_ = state->metrics;
    cancel();
    return value;
  }

private:
  friend class asset_manager;
  friend struct detail::asset_request_access;
  template <class> friend class asset_request;
  std::shared_ptr<detail::asset_state> state_;
  std::size_t source_bytes_{};
  asset_load_metrics metrics_;
};
}

namespace gneiss::detail {
/** 内部依赖调度订阅；回调只投递就绪通知，不运行用户代码。 */
struct asset_request_access {
  template <class T>
  static void when_ready(const asset_request<T>& request, std::function<void()> notify) {
    if (!request.state_ || !notify)
      return;
    {
      std::scoped_lock lock(request.state_->mutex);
      if (request.state_->status == error::not_ready) {
        request.state_->completed.push_back(std::move(notify));
        return;
      }
    }
    notify();
  }
};
}
