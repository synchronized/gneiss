// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include <memory>
#include <mutex>
#include <utility>

namespace gneiss::engine::resource {
namespace detail {
// 移植 Granit 示例 asset_state：请求共享结果，观察者各自拥有取消权。
struct asset_state {
  std::mutex mutex;
  gneiss::engine::core::result status{gneiss::engine::core::result::not_ready};
  std::shared_ptr<const void> value;
};
}
/** 内部移动请求；取消仅放弃本观察者，成功领取一次只读结果。
 * 不驱动队列、不等待 I/O；结果可晚于 manager 销毁。请求对象限所属线程操作。 */
template <typename T> class asset_request final {
public:
  using reference = std::shared_ptr<const T>;
  asset_request() noexcept = default;
  explicit asset_request(std::shared_ptr<detail::asset_state> state) noexcept
      : state_(std::move(state)) {}
  asset_request(const asset_request&) = delete;
  asset_request& operator=(const asset_request&) = delete;
  asset_request(asset_request&&) noexcept = default;
  asset_request& operator=(asset_request&&) noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
  void cancel() noexcept { state_.reset(); }
  [[nodiscard]] gneiss::engine::core::result take(reference& output) noexcept {
    if (!state_)
      return gneiss::engine::core::result::invalid_state;
    auto state = state_;
    const std::scoped_lock lock(state->mutex);
    if (state->status != gneiss::engine::core::result::success)
      return state->status;
    if (!state->value)
      return gneiss::engine::core::result::internal;
    output = std::static_pointer_cast<const T>(state->value);
    state_.reset();
    return gneiss::engine::core::result::success;
  }

private:
  std::shared_ptr<detail::asset_state> state_;
};
}
