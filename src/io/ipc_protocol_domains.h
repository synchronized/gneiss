// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_IPC_PROTOCOL_DOMAINS_H_
#define GNEISS_SRC_IO_IPC_PROTOCOL_DOMAINS_H_

#include "ipc_dispatcher.h"

#include <array>

namespace gneiss {

struct ipc_protocol_domain_handlers final {
  ipc_domain_handler session = nullptr;
  ipc_domain_handler control = nullptr;
  ipc_domain_handler log = nullptr;
  ipc_domain_handler inspection = nullptr;
  ipc_domain_handler statistics = nullptr;
  ipc_domain_handler property = nullptr;
  void* context = nullptr;
};

/** 当前 v2 标准域能力；顺序同时作为握手请求的稳定优先级。 */
[[nodiscard]] std::span<const ipc_domain_capability> ipc_v2_domain_capabilities() noexcept;

/** 原子建立当前 v2 标准域注册表；所有处理器必须有效。 */
[[nodiscard]] result register_ipc_v2_domains(const ipc_protocol_domain_handlers& handlers,
                                             ipc_domain_registry& output) noexcept;

} // namespace gneiss

#endif
