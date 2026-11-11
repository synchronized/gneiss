// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/logging/record.hpp"

namespace gneiss::logging {
/** 同步输出端，由日志运行时串行调用；不保存借用记录，不建立队列、不重入日志。
 * 可预期输出失败返回错误；分配及未知异常向执行边界传播。 */
class sink {
public:
  virtual ~sink() = default;
  [[nodiscard]] virtual error write(const record_view& event) = 0;
  [[nodiscard]] virtual error flush() = 0;
};
} // namespace gneiss::logging
