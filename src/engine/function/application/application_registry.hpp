// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_REGISTRY_HPP_
#define GNEISS_APPLICATION_APPLICATION_REGISTRY_HPP_
#include <exception>
#include <gneiss/engine/application.h>
#include <memory>
#include <utility>

namespace gneiss::engine {
class application;
}
namespace gneiss::application_internal {
struct application_configuration;
using application_resource = std::shared_ptr<engine::application>;
/** 私有借用解析；共享指针只保护当前调用，不延长已关闭服务的有效性。 */
[[nodiscard]] GNEISS_API application_resource
find_application(gneiss_application application) noexcept;
[[nodiscard]] gneiss_result validate_application(const application_resource& application) noexcept;
/** 输入已由 ABI 入口补齐旧布局并校验；唯一注册表在初始化成功后发布句柄。 */
[[nodiscard]] gneiss_result create_application(const application_configuration& config,
                                               gneiss_application& output) noexcept;
[[nodiscard]] gneiss_result destroy_application(gneiss_application application) noexcept;
/** 内部兼容桥；不持有 application，不授予关闭权。绑定、移动和撤销限所属线程。
 * 必须在最后一次兼容调用后销毁；对象先关闭时旧句柄立即不能再解析。 */
class application_bridge final {
public:
  application_bridge() noexcept = default;
  ~application_bridge() noexcept {
    if (reset() != GNEISS_SUCCESS) {
      std::terminate();
    }
  }
  application_bridge(const application_bridge&) = delete;
  application_bridge& operator=(const application_bridge&) = delete;
  application_bridge(application_bridge&& other) noexcept
      : handle_(std::exchange(other.handle_, GNEISS_NULL_APPLICATION)) {}
  application_bridge& operator=(application_bridge&&) = delete;
  [[nodiscard]] GNEISS_API gneiss_result bind(const application_resource& application) noexcept;
  [[nodiscard]] GNEISS_API gneiss_result reset() noexcept;
  [[nodiscard]] gneiss_application get() const noexcept { return handle_; }

private:
  gneiss_application handle_{};
};
} // namespace gneiss::application_internal
#endif
