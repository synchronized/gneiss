// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/logging/default_service.hpp>

#include "detail/service_state.hpp"
namespace gneiss::logging {
namespace {
struct default_route {
  std::mutex mutex;
  std::weak_ptr<detail::service_state> state;
};
default_route& route() {
  // 进程生命周期的小型路由槽，避免静态析构期间 Logger 访问已销毁的互斥量；不保活服务。
  static auto* value = new default_route;
  return *value;
}
}
std::shared_ptr<detail::service_state> detail::resolve_default_service() {
  auto& value = route();
  std::scoped_lock lock(value.mutex);
  return value.state.lock();
}
default_service_binding::default_service_binding(service& logs) {
  auto& value = route();
  std::scoped_lock lock(value.mutex);
  GNEISS_ASSERT(value.state.expired(), "默认日志服务已安装");
  {
    std::scoped_lock gate(logs.state_->mutex);
    GNEISS_ASSERT(logs.state_->active, "不能绑定已关闭日志服务");
  }
  state_ = logs.state_;
  value.state = state_;
}
default_service_binding::~default_service_binding() { reset(); }
void default_service_binding::reset() noexcept {
  auto& value = route();
  std::scoped_lock lock(value.mutex);
  if (!state_.owner_before(value.state) && !value.state.owner_before(state_))
    value.state.reset();
  state_.reset();
}
}
