// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <caors/resources/async.h>
#include <gneiss/engine/core/result.hpp>
#include <utility>

namespace gneiss::engine::resource {
/** 内部资源请求的移动所有者，T 是 CAORS 资源类型；不进入安装 SDK。
 * 复用 CAORS 订阅状态，不推进队列、不等待 I/O、不应用业务结果。
 * 所属线程操作；销毁/覆盖取消本订阅，须先于 manager 销毁。
 * 取出的强引用保持资源存活，消费者不得修改共享资产。 */
template <typename T> class asset_request final {
public:
  using reference = ::caors::resources::handle<T>;
  asset_request() noexcept = default;
  explicit asset_request(::caors::resources::load_request<T> request) noexcept
      : request_(std::move(request)) {}
  ~asset_request() { cancel(); }
  asset_request(const asset_request&) = delete;
  asset_request& operator=(const asset_request&) = delete;
  asset_request(asset_request&& other) noexcept : request_(std::exchange(other.request_, {})) {}
  asset_request& operator=(asset_request&& other) noexcept {
    if (this != &other) {
      cancel();
      request_ = std::exchange(other.request_, {});
    }
    return *this;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return request_.valid(); }
  void cancel() noexcept {
    (void)request_.cancel();
    request_ = {};
  }
  /** 非阻塞获取强引用；成功消费一次，失败不改 output。
   * 失败原因继续由 CAORS 请求保存；空、取消及已消费请求返回 invalid_state。 */
  [[nodiscard]] result take(reference& output) noexcept {
    if (!request_ || request_.cancelled())
      return result::invalid_state;
    if (!request_.done())
      return result::not_ready;
    if (!request_.loaded()) {
      const auto error = request_.error().value();
      return error == ::caors::errc::not_found          ? result::not_found
             : error == ::caors::errc::invalid_argument ? result::invalid_argument
             : error == ::caors::errc::out_of_memory    ? result::out_of_memory
                                                        : result::io;
    }
    auto value = request_.try_get();
    if (!value)
      return result::internal;
    output = std::move(value);
    request_ = {};
    return result::success;
  }

private:
  ::caors::resources::load_request<T> request_;
};
}
