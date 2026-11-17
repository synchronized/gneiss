// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <gneiss/engine/core/serialization/json.hpp>

namespace gneiss::protocol {
/** 编码独立 JSON 负载；limit 为协议域允许的字节上限。保留反射诊断。 */
template <class T>
[[nodiscard]] result<std::vector<std::uint8_t>, serialization::diagnostic>
encode_payload(const T& object, std::size_t limit) {
  auto encoded = serialization::json::encode_bytes(object);
  if (!encoded)
    return std::move(encoded).error();
  if (encoded->size() > limit)
    return serialization::diagnostic{.message = "协议负载超出大小限制",
                                     .error = error::invalid_argument};
  std::vector<std::uint8_t> bytes;
  bytes.reserve(encoded->size());
  for (auto byte : *encoded)
    bytes.push_back(std::to_integer<std::uint8_t>(byte));
  return bytes;
}
/** 先限制输入大小，再解码独立候选；业务约束由调用方验证。 */
template <class T>
[[nodiscard]] result<T, serialization::diagnostic>
decode_payload(std::span<const std::uint8_t> bytes, std::size_t limit) {
  if (bytes.size() > limit)
    return serialization::diagnostic{.message = "协议负载超出大小限制",
                                     .error = error::invalid_argument};
  return serialization::json::decode<T>(std::as_bytes(bytes));
}
}
