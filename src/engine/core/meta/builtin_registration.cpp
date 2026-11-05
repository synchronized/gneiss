// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/detail/value_codec.hpp"
namespace gneiss::engine::meta {
error register_builtin_types(const registry_ref& registry) noexcept {
  return type_builder<value>{registry}
      .serialization<&serialization::detail::encode_value, &serialization::detail::decode_value>()
      .validate<[](const value& v) {
        return v.kind() == value_kind::object || valid_scalar(v) ? error::success
                                                                 : error::invalid_argument;
      }>()
      .commit();
}
}
