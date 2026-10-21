// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPLICATION_APPLICATION_REGISTRY_HPP_
#define GNEISS_APPLICATION_APPLICATION_REGISTRY_HPP_
#include "runtime/application_bridge.hpp"
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
} // namespace gneiss::application_internal
#endif
