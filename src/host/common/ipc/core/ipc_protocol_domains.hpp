// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_IPC_CORE_IPC_PROTOCOL_DOMAINS_H_
#define GNEISS_APPS_COMMON_IPC_CORE_IPC_PROTOCOL_DOMAINS_H_

#include "engine/core/result.hpp"
#include "ipc_dispatcher.hpp"

#include <array>

namespace gneiss::host {

/** 当前 v2 标准域能力；顺序同时作为握手请求的稳定优先级。 */
[[nodiscard]] std::span<const gneiss::engine::ipc_domain_capability>
ipc_v2_domain_capabilities() noexcept;

/** 使用统一入口原子建立当前 v2 标准域注册表。 */
[[nodiscard]] gneiss::engine::result
register_ipc_v2_domains(gneiss::engine::ipc_domain_handler handler, void* handler_context,
                        gneiss::engine::ipc_domain_registry& output) noexcept;

} // namespace gneiss::host

#endif
