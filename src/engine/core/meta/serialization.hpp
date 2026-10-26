// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/type_registry.hpp"
#include <cstddef>
#include <new>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
namespace gneiss::engine::meta {
/** 编解码错误位置；拥有文本，不依赖资源、文件系统或后端。 */
struct serialization_diagnostic {
  std::size_t byte_offset{};
  std::string path;
  std::string message;
};
/** 每种类型可选一组编解码函数，文件格式及版本由 codec 识别。对象及字节仅在调用期间借用。
 * 回调必须可并发操作不同对象；模块需在 Registry 使用期间存活。 */
struct serialization_binding {
  result (*encode)(const void*, std::vector<std::byte>&, serialization_diagnostic&){};
  result (*decode)(std::span<const std::byte>, void*, serialization_diagnostic&){};
};
/** 冻结后查询函数表；失败保持输出，未登记能力返回 unsupported。 */
[[nodiscard]] GNEISS_API result find_serialization(const registry_ref&, type_id,
                                                   serialization_binding&) noexcept;
/** 成功才替换输出；对象必须与模板类型相符，未知类型不隐式注册。 */
template <class T>
[[nodiscard]] result serialize(const registry_ref& registry, const T& object,
                               std::vector<std::byte>& output,
                               serialization_diagnostic& diagnostic) noexcept {
  diagnostic = {};
  serialization_binding binding;
  auto status = find_serialization(registry, runtime_type_of<T>(), binding);
  if (status.failed())
    return status;
  if (!binding.encode)
    return result::unsupported;
  try {
    std::vector<std::byte> pending;
    status = binding.encode(&object, pending, diagnostic);
    if (status == result::success)
      output = std::move(pending);
    return status;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
/** 解码到临时值；成功后无异常移动赋值，拒绝把半成品写入既有对象。 */
template <class T>
[[nodiscard]] result deserialize(const registry_ref& registry, std::span<const std::byte> bytes,
                                 T& output, serialization_diagnostic& diagnostic) noexcept {
  static_assert(std::is_nothrow_move_assignable_v<T>);
  diagnostic = {};
  serialization_binding binding;
  auto status = find_serialization(registry, runtime_type_of<T>(), binding);
  if (status.failed())
    return status;
  if (!binding.decode)
    return result::unsupported;
  try {
    T pending{};
    status = binding.decode(bytes, &pending, diagnostic);
    if (status == result::success)
      output = std::move(pending);
    return status;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
}
