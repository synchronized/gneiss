// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/logging/sink.hpp>
#include <memory>
namespace gneiss::logging {
/** 同步 stderr 输出。宿主服务串行调用；flush 时统一刷新。 */
[[nodiscard]] std::unique_ptr<sink> create_console_sink();
}
