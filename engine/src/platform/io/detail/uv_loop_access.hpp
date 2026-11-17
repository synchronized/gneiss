// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/platform/io/executor.hpp>

#include <uv.h>

#include <functional>

namespace gneiss::io::detail {

/** 仅供 I/O 后端 `.cpp` 使用；不得进入普通接口或业务头文件。 */
class uv_loop_access final {
public:
  using task = std::function<void(uv_loop_t*)>;

  [[nodiscard]] static error post(executor& executor, task operation);
};

} // namespace gneiss::io::detail
