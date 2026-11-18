// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <algorithm>
#include <array>
#include <gneiss/engine/assert.hpp>
#include <gneiss/engine/core/serialization/adapter.hpp>
#include <optional>

namespace gneiss::serialization {
namespace detail {
struct guid_adapter {
  using representation_type = std::optional<std::string>;
  static result<representation_type> encode(const write_context&, const guid& input) {
    return input ? representation_type{input.string()} : representation_type{};
  }
  static result<guid> decode(const read_context&, const representation_type& input) {
    if (!input)
      return guid{};
    auto id = guid::parse(*input);
    if (!id || id->string() != *input)
      return error::invalid_argument;
    return *id;
  }
};
template <class T> struct math_adapter {
  using representation_type =
      std::array<float, decltype(math::components(std::declval<T&>()))::extent>;
  static result<representation_type> encode(const write_context&, const T& input) {
    representation_type output;
    std::ranges::copy(math::components(input), output.begin());
    return output;
  }
  static result<T> decode(const read_context&, const representation_type& input) {
    T output{};
    std::ranges::copy(input, math::components(output).begin());
    return output;
  }
};
}
namespace detail {
const adapter_binding* find_adapter(const context_base& ctx, type_id id) {
  if (ctx.adapters())
    if (const auto* custom = ctx.adapters()->find(id))
      return custom;
  static const adapter_registry builtins = [] {
    adapter_registry output;
    GNEISS_ASSERT((register_adapter<guid, guid_adapter>(output).ok()));
    GNEISS_ASSERT((register_adapter<math::vector2, math_adapter<math::vector2>>(output).ok()));
    GNEISS_ASSERT((register_adapter<math::vector3, math_adapter<math::vector3>>(output).ok()));
    GNEISS_ASSERT((register_adapter<math::vector4, math_adapter<math::vector4>>(output).ok()));
    GNEISS_ASSERT(
        (register_adapter<math::quaternion, math_adapter<math::quaternion>>(output).ok()));
    GNEISS_ASSERT((register_adapter<math::matrix3, math_adapter<math::matrix3>>(output).ok()));
    GNEISS_ASSERT((register_adapter<math::matrix4, math_adapter<math::matrix4>>(output).ok()));
    return output;
  }();
  return builtins.find(id);
}
}
}
