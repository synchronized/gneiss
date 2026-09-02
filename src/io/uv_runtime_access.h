// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_IO_UV_RUNTIME_ACCESS_H_
#define GNEISS_SRC_IO_UV_RUNTIME_ACCESS_H_

#include "uv_runtime.h"

#include <uv.h>

#include <functional>

namespace gneiss {

/** 仅供 I/O 后端 `.cpp` 使用；不得进入普通接口或业务头文件。 */
class uv_runtime_access final {
public:
  using task = std::function<void(uv_loop_t*)>;

  [[nodiscard]] static result post(uv_runtime& runtime, task operation) noexcept;
};

} // namespace gneiss

#endif
