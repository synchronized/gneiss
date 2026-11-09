// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/serialization/context.hpp"
#include "engine/core/serialization/reader.hpp"
#include "engine/core/serialization/writer.hpp"
#include "engine/resource/asset.hpp"
#include "engine/resource/asset_uri.hpp"
namespace gneiss::engine::detail {
template <class T>
result<asset_reference_value> capture_asset_reference(guid type, const void* pointer) {
  const auto& uri = static_cast<const T*>(pointer)->source().uri;
  if (validate_uri(uri).failed())
    return error::invalid_argument;
  return asset_reference_value{type, uri};
}

template <class T>
result<void*, serialization::diagnostic> encode_asset_reference(const serialization::write_context&,
                                                                serialization::writer& out,
                                                                const void* pointer) {
  const auto& uri = static_cast<const T*>(pointer)->source().uri;
  if (validate_uri(uri).failed())
    return serialization::diagnostic{.message = "资源引用没有可保存的 URI",
                                     .error = error::invalid_argument};
  auto node = out.object();
  auto text = out.string(uri);
  if (!node || !text || !out.member(node, "$asset", text))
    return serialization::diagnostic{.error = error::out_of_memory};
  return node;
}
template <class T>
result<ref<void>, serialization::diagnostic>
decode_asset_reference(const serialization::read_context& ctx, serialization::reader& input,
                       const void* node) {
  if (!input.is_object(node) || input.size(node) != 1)
    return serialization::diagnostic{.message = "资源引用必须只有 $asset 字段",
                                     .error = error::invalid_argument};
  auto uri = input.member(node, "$asset");
  if (!input.is_string(uri) || validate_uri(input.string(uri)).failed())
    return serialization::diagnostic{.path = "/$asset", .error = error::invalid_argument};
  if (!ctx.references())
    return serialization::diagnostic{.message = "资源引用需要解析上下文",
                                     .error = error::invalid_state};
  return ctx.references()->resolve(runtime_type_of<T>(), input.string(uri), ctx.property_path());
}
}
