// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/result.hpp>
#include <gneiss/engine/core/serialization/policy.hpp>
#include <gneiss/engine/function/asset/asset_loading.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
namespace gneiss::test_support {
inline gneiss::error take_value(gneiss::result<gneiss::scene_instantiation_result> value,
                                scene_instance& output) {
  output = value ? value->instance : scene_instance{};
  return value ? gneiss::error{gneiss::error::success} : value.error();
}
inline gneiss::error assign_value(gneiss::result<gneiss::game_object> value, entity_id& output) {
  output = value ? value->entity() : entity_id{};
  return value ? gneiss::error::success : value.error();
}
template <class T, class U>
  requires std::is_assignable_v<U&, T&&>
gneiss::error take_value(gneiss::result<T> value, U& output) {
  if (!value) {
    output = {};
    return value.error();
  }
  output = std::move(*value);
  return gneiss::error::success;
}
// 旧测试在失败时保持已有引用；只用于适配断言编排。
template <class T> gneiss::error assign_value(gneiss::result<T> value, T& output) {
  if (!value)
    return value.error();
  output = std::move(*value);
  return gneiss::error::success;
}
template <class T>
gneiss::error assign_value(gneiss::result<T, gneiss::serialization::diagnostic> value, T& output) {
  if (!value)
    return value.error().error;
  output = std::move(*value);
  return gneiss::error::success;
}
template <class T, class E>
gneiss::error assign_decoded(gneiss::result<T, E> value, T& output, E& diagnostic) {
  if (!value) {
    diagnostic = std::move(value).error();
    return diagnostic.error;
  }
  diagnostic = {};
  output = std::move(*value);
  return gneiss::error::success;
}

template <class T, class E> gneiss::error assign_decoded(gneiss::result<T, E> value, T& output) {
  if (!value)
    return value.error().error;
  output = std::move(*value);
  return gneiss::error::success;
}
template <class R, class T>
gneiss::error take_request(R& request, T& output,
                           gneiss::serialization::diagnostic* diagnostic = nullptr,
                           gneiss::asset_load_metrics* metrics = nullptr) {
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
  return gneiss::error::success;
}
}

namespace gneiss::test_support {
// 只适配测试断言的输出形式；不拥有请求、不推进加载。
template <class Request, class Completion>
gneiss::error receive_request(Request& request, Completion& output, bool& ready) {
  ready = false;
  if (!request)
    return gneiss::error::success;
  auto value = request.take();
  if (!value)
    return value.error() == gneiss::error::not_ready ? gneiss::error::success : value.error();
  output = std::move(*value);
  ready = true;
  return gneiss::error::success;
}
}
