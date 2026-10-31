// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_load_types.hpp"
#include "engine/function/resource/asset_types.hpp"
namespace gneiss::engine {
namespace detail {
struct resource_load_state {
  render_asset_ref target;
  error status{error::not_ready};
  asset_diagnostic diagnostic;
  asset_data_metrics metrics;
};
}
/** 单资源请求；所属线程查询。复制共享观察权，cancel 仅释放当前观察权。
 * 结果表示 CPU 内容和依赖已采用，不等待 GPU。服务停止后仍可查询失败结果。 */
class resource_request final {
public:
  explicit operator bool() const noexcept { return bool(state_); }
  error status() const noexcept { return state_ ? state_->status : error::invalid_state; }
  render_asset_ref resource() const noexcept {
    return state_ ? state_->target : render_asset_ref{};
  }
  template <class T> asset_ref<const T> get() const noexcept { return asset_cast<T>(resource()); }
  asset_diagnostic diagnostic() const { return state_ ? state_->diagnostic : asset_diagnostic{}; }
  const asset_data_metrics& metrics() const noexcept {
    static const asset_data_metrics empty;
    return state_ ? state_->metrics : empty;
  }
  void cancel() noexcept { state_.reset(); }

private:
  friend class resource_service;
  std::shared_ptr<detail::resource_load_state> state_;
};
}
