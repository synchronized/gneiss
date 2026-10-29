// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/registry.hpp"
#include <cstddef>
#include <new>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace gneiss::engine::meta {
/** 成功才替换输出；对象必须与模板类型相符，未知类型不隐式注册。 */
template <class T>
[[nodiscard]] error serialize(const registry_ref& registry, const T& object,
                              std::vector<std::byte>& output,
                              serialization_diagnostic& diagnostic) noexcept {
  diagnostic = {};
  auto binding = find_serialization(registry, runtime_type_of<T>());
  if (!binding)
    return binding.error();
  if (!binding->encode)
    return error::unsupported;
  try {
    std::vector<std::byte> pending;
    auto status = binding->encode(&object, pending, diagnostic);
    if (status == error::success)
      output = std::move(pending);
    return status;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}
/** 解码到临时值；成功后无异常移动赋值，拒绝把半成品写入既有对象。 */
template <class T>
[[nodiscard]] error deserialize(const registry_ref& registry, std::span<const std::byte> bytes,
                                T& output, serialization_diagnostic& diagnostic) noexcept {
  static_assert(std::is_nothrow_move_assignable_v<T>);
  diagnostic = {};
  auto binding = find_serialization(registry, runtime_type_of<T>());
  if (!binding)
    return binding.error();
  if (!binding->decode)
    return error::unsupported;
  try {
    T pending{};
    auto status = binding->decode(bytes, &pending, diagnostic);
    if (status == error::success)
      output = std::move(pending);
    return status;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}
}
