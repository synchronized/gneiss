// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
namespace gneiss::test_support {
template <class T> engine::error take_value(engine::result<T> value, T& output) {
  if (!value) {
    output = {};
    return value.error();
  }
  output = std::move(*value);
  return engine::error::success;
}
// 旧测试在失败时保持已有引用；只用于适配断言编排。
template <class T> engine::error assign_value(engine::result<T> value, T& output) {
  if (!value)
    return value.error();
  output = std::move(*value);
  return engine::error::success;
}
template <class T, class E>
engine::error assign_decoded(engine::result<T, E> value, T& output, E& diagnostic) {
  if (!value) {
    diagnostic = std::move(value).error();
    return diagnostic.result;
  }
  output = std::move(*value);
  return engine::error::success;
}
}
