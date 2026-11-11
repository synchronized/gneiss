// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "engine/core/serialization/context.hpp"
namespace gneiss::serialization::detail {
/** 所有格式共用类型分派；对象仍由注册工厂构造，不要求默认构造。 */
template <class T, class DecodeView, class DecodeObject>
result<T, diagnostic> decode_typed(const read_context& ctx, DecodeView&& decode_view,
                                   DecodeObject&& decode_object) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  if constexpr (std::is_default_constructible_v<T>) {
    if (meta::detail::ops_of<T>().kind() != meta::type_kind::object) {
      T object{};
      auto view = meta::view(*registry, object);
      if (!view)
        return diagnostic{.error = view.error()};
      auto status = decode_view(*view);
      if (!status)
        return std::move(status).error();
      return object;
    }
  }
  auto decoded = decode_object(runtime_type_of<T>());
  if (!decoded)
    return std::move(decoded).error();
  if (!*decoded)
    return diagnostic{.error = error::internal};
  return std::move(*static_cast<T*>(decoded->get()));
}
}
