// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/asset_ref.hpp"
#include <memory>
#include <mutex>
#include <utility>

namespace gneiss::engine {
namespace detail {
// 移植 Granit 示例 asset_state：请求共享结果，观察者各自拥有取消权。
struct asset_state {
  std::mutex mutex;
  error status{error::not_ready};
  std::shared_ptr<const void> value;
  meta::serialization_diagnostic diagnostic;
};
}
/** 内部移动请求；取消仅放弃本观察者，成功领取一次只读结果。
 * 不驱动队列、不等待 I/O；结果可晚于 manager 销毁。请求对象限所属线程操作。 */
template <typename T> class asset_request final {
public:
  using value_type = T;
  using reference = asset_ref<const T>;
  asset_request() noexcept = default;
  explicit asset_request(std::shared_ptr<detail::asset_state> state) noexcept
      : state_(std::move(state)) {}
  asset_request(const asset_request&) = delete;
  asset_request& operator=(const asset_request&) = delete;
  asset_request(asset_request&&) noexcept = default;
  asset_request& operator=(asset_request&&) noexcept = default;
  [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
  void cancel() noexcept { state_.reset(); }
  /** 非阻塞领取；not_ready 保留请求，成功后请求失效。失败携带解析诊断。 */
  [[nodiscard]] result<reference, asset_diagnostic> take() noexcept try {
    if (!state_)
      return asset_diagnostic{error::invalid_state, 0, {}, {}};
    auto state = state_;
    const std::scoped_lock lock(state->mutex);
    if (state->status != error::success)
      return asset_diagnostic{state->status, state->diagnostic.byte_offset, state->diagnostic.path,
                              state->diagnostic.message};
    if (!state->value)
      return asset_diagnostic{error::internal, 0, {}, {}};
    auto value = reference{std::static_pointer_cast<const T>(state->value)};
    state_.reset();
    return value;
  } catch (const std::bad_alloc&) {
    return asset_diagnostic{error::out_of_memory, 0, {}, {}};
  } catch (...) {
    return asset_diagnostic{error::internal, 0, {}, {}};
  }

private:
  std::shared_ptr<detail::asset_state> state_;
};
}
