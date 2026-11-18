// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <gneiss/engine/core/meta/ops/scalar_ops.hpp>
#include <optional>

namespace gneiss::meta {
/** 固定类型表示；特化须在首次生成类型能力前可见，各编译单元保持一致。 */
template <class T> struct representation_traits {
  static constexpr bool enabled = false;
};
template <> struct representation_traits<guid> {
  static constexpr bool enabled = true;
  using representation_type = std::optional<std::string>;
  static result<representation_type> encode(const guid& input) {
    return input ? representation_type{input.string()} : representation_type{};
  }
  static result<guid> decode(const representation_type& input) {
    if (!input)
      return guid{};
    auto id = guid::parse(*input);
    if (!id || id->string() != *input)
      return error::invalid_argument;
    return *id;
  }
};
namespace detail {
template <class T> struct math_representation {
  static constexpr bool enabled = true;
  using representation_type =
      std::array<float, decltype(math::components(std::declval<T&>()))::extent>;
  static result<representation_type> encode(const T& input) {
    representation_type output;
    std::ranges::copy(math::components(input), output.begin());
    return output;
  }
  static result<T> decode(const representation_type& input) {
    T output{};
    std::ranges::copy(input, math::components(output).begin());
    return output;
  }
};
}
template <>
struct representation_traits<math::vector2> : detail::math_representation<math::vector2> {};
template <>
struct representation_traits<math::vector3> : detail::math_representation<math::vector3> {};
template <>
struct representation_traits<math::vector4> : detail::math_representation<math::vector4> {};
template <>
struct representation_traits<math::quaternion> : detail::math_representation<math::quaternion> {};
template <>
struct representation_traits<math::matrix3> : detail::math_representation<math::matrix3> {};
template <>
struct representation_traits<math::matrix4> : detail::math_representation<math::matrix4> {};

/** 回调中的地址仅在调用期间有效；转换不修改已有实例，也不依赖注册数据。 */
struct representation_ops {
  using const_visitor = std::function<error(const void*, const type_ops&)>;
  using visitor = std::function<error(void*, const type_ops&)>;
  error (*encode)(const void*, const const_visitor&);
  error (*decode)(const visitor& read, const visitor& consume);
  error (*encode_value)(const value&, const const_visitor&){};
};
namespace detail {
template <class T> const representation_ops& representation_ops_of() {
  using A = representation_traits<T>;
  using R = typename A::representation_type;
  static_assert(!std::is_same_v<T, R>, "Representation must differ from its source");
  static const representation_ops ops = [] {
    representation_ops output;
    output.encode = [](const void* source,
                       const representation_ops::const_visitor& visit) -> error {
      auto encoded = A::encode(*static_cast<const T*>(source));
      if (!encoded)
        return encoded.error();
      return visit(std::addressof(*encoded), ops_of<R>());
    };
    output.decode = [](const representation_ops::visitor& read,
                       const representation_ops::visitor& consume) -> error {
      R representation{};
      auto status = read(std::addressof(representation), ops_of<R>());
      if (status.failed())
        return status;
      auto decoded = A::decode(std::as_const(representation));
      if (!decoded)
        return decoded.error();
      return consume(std::addressof(*decoded), ops_of<T>());
    };
    if constexpr (reflected_value_kind<T>() != value_kind::invalid) {
      output.encode_value = [](const value& source,
                               const representation_ops::const_visitor& visit) -> error {
        auto native = scalar_cast<T>(source);
        if (!native)
          return native.error();
        auto encoded = A::encode(*native);
        if (!encoded)
          return encoded.error();
        return visit(std::addressof(*encoded), ops_of<R>());
      };
    }
    return output;
  }();
  return ops;
}
}
}
