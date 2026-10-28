// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "asset_identity.hpp"
#include "asset_request.hpp"
#include "engine/core/sha256.hpp"
#include <optional>
#include <vector>
namespace gneiss::engine {
class asset_manager;
/** 本次实际读取的来源快照，不是缓存版本或事务标识。 */
struct asset_data_source {
  asset_identity identity;
  std::uint64_t bytes{};
  sha256_digest digest{};
};
struct asset_data_metrics {
  std::vector<asset_data_source> sources;
  std::size_t bytes{};
  double decode_ms{};
  double queue_ms{};
};
namespace detail {
template <class T> struct asset_data_state : asset_state {
  std::optional<T> data;
  asset_data_metrics metrics;
};
}
/** 一次性 CPU 内容请求；成功移动领取，不保留长期缓存。
 * 所属线程使用，取消丢弃观察权，不等待正在运行的解码任务。 */
template <class T> class asset_data_request final {
public:
  asset_data_request() = default;
  asset_data_request(asset_data_request&&) noexcept = default;
  asset_data_request& operator=(asset_data_request&&) noexcept = default;
  asset_data_request(const asset_data_request&) = delete;
  asset_data_request& operator=(const asset_data_request&) = delete;
  explicit operator bool() const noexcept { return bool(state_); }
  void cancel() noexcept { state_.reset(); }
  result take(T& output, meta::serialization_diagnostic* diagnostic = nullptr,
              asset_data_metrics* metrics = nullptr) noexcept try {
    if (!state_)
      return result::invalid_state;
    auto state = state_;
    const std::scoped_lock lock(state->mutex);
    if (diagnostic)
      *diagnostic = state->diagnostic;
    if (state->status.failed())
      return state->status;
    if (!state->data)
      return result::internal;
    output = std::move(*state->data);
    if (metrics)
      *metrics = std::move(state->metrics);
    state_.reset();
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }

private:
  friend class asset_manager;
  std::shared_ptr<detail::asset_data_state<T>> state_;
};
}
