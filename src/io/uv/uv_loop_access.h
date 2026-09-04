// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_UV_UV_LOOP_ACCESS_H_
#define GNEISS_SRC_IO_UV_UV_LOOP_ACCESS_H_

#include "uv_loop_executor.h"

#include <uv.h>

#include <functional>

namespace gneiss::io_internal {

/** 仅供 I/O 后端 `.cpp` 使用；不得进入普通接口或业务头文件。 */
class uv_loop_access final {
public:
  using task = std::function<void(uv_loop_t*)>;

  [[nodiscard]] static result post(uv_loop_executor& executor, task operation) noexcept;
};

} // namespace gneiss::io_internal

#endif
