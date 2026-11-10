// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/core/serialization/policy.hpp"
#include "engine/function/asset/asset_loading.hpp"
#include "engine/function/scene/instantiation.hpp"
namespace gneiss::test_support {
inline engine::error take_value(engine::result<engine::scene_instantiation_result> value,
                                scene_instance_id& output) {
  output = value ? value->instance : scene_instance_id{};
  return value ? engine::error{engine::error::success} : value.error();
}
inline engine::error assign_value(engine::result<engine::game_object> value, entity_id& output) {
  output = value ? value->entity() : entity_id{};
  return value ? engine::error::success : value.error();
}
template <class T, class U>
  requires std::is_assignable_v<U&, T&&>
engine::error take_value(engine::result<T> value, U& output) {
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
template <class T>
engine::error assign_value(engine::result<T, engine::serialization::diagnostic> value, T& output) {
  if (!value)
    return value.error().error;
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
                           engine::serialization::diagnostic* diagnostic = nullptr,
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

namespace gneiss::test_support {
template <class T, class U> engine::error read_component_value(const T* value, U& out) {
  if (!value)
    return engine::error::not_found;
  if constexpr (requires { value->settings(); })
    out = value->settings();
  else
    out = value->value;
  return engine::error::success;
}
}

namespace gneiss::test_support {
// 只适配测试断言的输出形式；不拥有请求、不推进加载。
template <class Request, class Progress>
bool observe_request(const Request& request, Progress& output) {
  auto value = request.progress();
  if (!value)
    return false;
  output = *value;
  return true;
}
template <class Request, class Completion>
engine::error receive_request(Request& request, Completion& output, bool& ready) {
  ready = false;
  if (!request)
    return engine::error::success;
  auto value = request.take();
  if (!value)
    return value.error() == engine::error::not_ready ? engine::error::success : value.error();
  output = std::move(*value);
  ready = true;
  return engine::error::success;
}
}
