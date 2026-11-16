// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/serialization/json.hpp>
#include <yyjson.h>

#include <cstdlib>
#include <memory>
#include <new>

namespace gneiss::host::detail {
/** 将引擎编码的完整 JSON 片段复制到 IPC 文档，不暴露引擎内部 DOM 适配器。 */
template <class T> yyjson_mut_val* write_reflected_json(yyjson_mut_doc* doc, const T& value) {
  auto encoded = serialization::json::encode(value);
  if (!encoded)
    return nullptr;
  auto* node = yyjson_mut_rawncpy(doc, encoded->data(), encoded->size());
  if (!node)
    throw std::bad_alloc{};
  return node;
}
/** DOM 片段通过格式入口解码；输出为独立候选，失败不修改调用方对象。 */
template <class T> result<T, serialization::diagnostic> read_reflected_json(yyjson_val* node) {
  if (!node)
    return serialization::diagnostic{.error = error::invalid_argument};
  std::size_t size{};
  yyjson_write_err failure{};
  std::unique_ptr<char, decltype(&std::free)> text(
      yyjson_val_write_opts(node, 0, nullptr, &size, &failure), &std::free);
  if (!text) {
    if (failure.code == YYJSON_WRITE_ERROR_MEMORY_ALLOCATION)
      throw std::bad_alloc{};
    return serialization::diagnostic{.error = error::invalid_argument};
  }
  return serialization::json::decode<T>(std::string_view{text.get(), size});
}
}
