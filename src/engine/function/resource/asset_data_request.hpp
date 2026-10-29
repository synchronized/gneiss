// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "asset_request.hpp"
#include "engine/core/sha256.hpp"
#include "engine/resource/asset_identity.hpp"
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
/** 一次领取的数据及其读取统计，随同一个结果转移所有权。 */
template <class T> struct asset_data {
  T data;
  asset_data_metrics metrics;
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
  /** 非阻塞领取内容和统计；成功消费请求，失败保留请求及详细诊断。 */
  [[nodiscard]] result<asset_data<T>, asset_diagnostic> take() noexcept try {
    if (!state_)
      return asset_diagnostic{error::invalid_state, 0, {}, {}};
    auto state = state_;
    const std::scoped_lock lock(state->mutex);
    if (state->status.failed())
      return asset_diagnostic{state->status, state->diagnostic.byte_offset, state->diagnostic.path,
                              state->diagnostic.message};
    if (!state->data)
      return asset_diagnostic{error::internal, 0, {}, {}};
    asset_data<T> value{std::move(*state->data), std::move(state->metrics)};
    state_.reset();
    return value;
  } catch (const std::bad_alloc&) {
    return asset_diagnostic{error::out_of_memory, 0, {}, {}};
  } catch (...) {
    return asset_diagnostic{error::internal, 0, {}, {}};
  }

private:
  friend class asset_manager;
  std::shared_ptr<detail::asset_data_state<T>> state_;
};
}
