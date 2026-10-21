// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <utility>
namespace example {
/** 示例局部清理动作，不提供引擎对象或业务接口。 */
template <class F> class scope_exit {
public:
  explicit scope_exit(F cleanup) : cleanup_(std::move(cleanup)) {}
  ~scope_exit() { cleanup_(); }
  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;

private:
  F cleanup_;
};
}
