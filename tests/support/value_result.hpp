// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/result.hpp"
#include "engine/function/resource/asset_loading.hpp"
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
    return diagnostic.error;
  }
  diagnostic = {};
  output = std::move(*value);
  return engine::error::success;
}

template <class T, class E> engine::error assign_decoded(engine::result<T, E> value, T& output) {
  if (!value)
    return value.error().error;
  output = std::move(*value);
  return engine::error::success;
}
template <class R, class T>
engine::error take_request(R& request, T& output,
                           engine::meta::serialization_diagnostic* diagnostic = nullptr,
                           engine::asset_load_metrics* metrics = nullptr) {
  auto value = request.take();
  if (!value) {
    auto failure = std::move(value).error();
    if (diagnostic)
      *diagnostic = {failure.byte_offset, std::move(failure.path), std::move(failure.message),
                     failure.error};
    return failure.error;
  }
  if (diagnostic)
    *diagnostic = {};
  output = std::move(*value);
  if constexpr (requires { request.metrics(); })
    if (metrics)
      *metrics = request.metrics();
  return engine::error::success;
}
}
