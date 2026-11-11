// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/service.hpp"
namespace gneiss::engine::logging {
/** 宿主作用域安装默认路由；重复安装报错。不得在生产者运行期间替换路由。 */
class GNEISS_API default_service_binding final {
public:
  explicit default_service_binding(service&);
  ~default_service_binding();
  default_service_binding(const default_service_binding&) = delete;
  default_service_binding& operator=(const default_service_binding&) = delete;
  void reset() noexcept;

private:
  std::weak_ptr<detail::service_state> state_;
};
}
