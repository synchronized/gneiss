// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>
#include <gneiss/engine/core/serialization/context.hpp>
#include <optional>

namespace gneiss::serialization::detail {
/** 所有格式共用类型分派；静态表示可直接构造候选，其余对象使用注册工厂。 */
template <class T, class DecodeView, class DecodeObject>
result<T, diagnostic> decode_typed(const read_context& ctx, DecodeView&& decode_view,
                                   DecodeObject&& decode_object) {
  auto registry = ctx.registry();
  if (!registry)
    return diagnostic{.error = registry.error()};
  if constexpr (std::is_default_constructible_v<T>) {
    if (meta::detail::ops_of<T>().kind() != meta::type_kind::object ||
        (meta::representation_traits<T>::enabled && !meta::type_of<T>(*registry).registered())) {
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
  if constexpr (meta::representation_traits<T>::enabled && !std::is_default_constructible_v<T>) {
    const auto type = meta::type_of<T>(*registry);
    if (!type.registered()) {
      std::optional<T> candidate;
      diagnostic issue;
      auto status = type.template capability<gneiss::meta::representation_capability>().decode(
          [&](const meta::instance& representation) {
            auto decoded = decode_view(representation);
            if (!decoded)
              issue = std::move(decoded).error();
            return issue.error;
          },
          [&](const meta::instance& object) {
            auto address = object.object_data();
            if (!address)
              return address.error();
            candidate.emplace(std::move(*static_cast<T*>(*address)));
            return error::success;
          });
      if (issue.error.failed())
        return issue;
      if (status.failed())
        return diagnostic{.error = status};
      return std::move(*candidate);
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
