// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_TYPE_IDENTITY_HPP_
#define GNEISS_ENGINE_TYPE_IDENTITY_HPP_

#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/math/camera.hpp>
#include <gneiss/engine/core/math/types.hpp>
#include <gneiss/engine/core/traits/type_name.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace gneiss {

/**
 * @brief C++ 类型的静态名称和稳定 GUID 定制点。
 *
 * 优先读取类内静态 type_name/type_guid；名称回退到 type_name<T>，GUID 默认为空。
 * cv/ref 统一委托规范化类型的特化。模块可以对第二模板参数做受约束偏特化，
 * 用户类型也可以直接特化本 traits。
 */
template <typename T, typename = void> struct type_identity {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;

  [[nodiscard]] static std::string_view name() noexcept {
    if constexpr (requires { std::string_view{type::type_name}; }) {
      return type::type_name;
    } else if constexpr (has_type_name_v<type>) {
      return type_name<type>::value;
    } else {
      return {};
    }
  }

  [[nodiscard]] static constexpr gneiss::guid guid() noexcept {
    if constexpr (requires { gneiss::guid{type::type_guid}; }) {
      return gneiss::guid{type::type_guid};
    } else {
      return gneiss::guid::nil();
    }
  }
};

template <class T>
  requires(!std::is_same_v<T, std::remove_cvref_t<T>>)
struct type_identity<T> : type_identity<std::remove_cvref_t<T>> {};

/** 当前内建反射字段的持久化身份，沿用既有 GUID。 */
template <> struct type_identity<math::vector3> {
  static constexpr std::string_view name() noexcept { return "vec3"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("90bcc813-a53c-4196-aa35-93f8946e82cb");
  }
};
template <> struct type_identity<math::vector2> {
  static constexpr std::string_view name() noexcept { return "vec2"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("1e1a6eaf-f385-409c-b365-d98be45f47d4");
  }
};
template <> struct type_identity<math::vector4> {
  static constexpr std::string_view name() noexcept { return "vec4"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("f4cae1c0-3da1-4fe7-ac62-a0711b4c9919");
  }
};
template <> struct type_identity<math::matrix3> {
  static constexpr std::string_view name() noexcept { return "matrix3"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("6d5184fd-12b1-468f-ab48-f6584b83436c");
  }
};
template <> struct type_identity<math::matrix4> {
  static constexpr std::string_view name() noexcept { return "matrix4"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("67dec1c6-2a38-4f05-af43-5233bdc8713c");
  }
};
template <> struct type_identity<math::quaternion> {
  static constexpr std::string_view name() noexcept { return "quaternion"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("21c111bd-d754-44f0-89c1-f7cc2775d91e");
  }
};
template <> struct type_identity<float> {
  static constexpr std::string_view name() noexcept { return "float"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("a27e9902-f3cb-4845-a7e8-3217f9759fc5");
  }
};
template <> struct type_identity<bool> {
  static constexpr std::string_view name() noexcept { return "bool"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("c2af2ad1-a625-4842-b4dd-74e977067e35");
  }
};

template <> struct type_identity<std::int64_t> {
  static constexpr std::string_view name() noexcept { return "int64"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("813f73d9-205e-42f2-933b-c3efb090caf1");
  }
};
template <> struct type_identity<std::uint64_t> {
  static constexpr std::string_view name() noexcept { return "uint64"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("2ec237db-7de3-41dc-a077-1e5dac89181c");
  }
};
template <> struct type_identity<double> {
  static constexpr std::string_view name() noexcept { return "double"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("bb42b86e-4a80-4695-9978-14d62ceeb217");
  }
};
template <> struct type_identity<std::string> {
  static constexpr std::string_view name() noexcept { return "string"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("60542978-c490-4ab5-8350-a901ecc98ea8");
  }
};
template <> struct type_identity<gneiss::guid> {
  static constexpr std::string_view name() noexcept { return "guid"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("5f7ad29c-1aa6-434f-9ec8-80ecdf59e182");
  }
};

template <> struct type_identity<math::camera_settings> {
  static constexpr std::string_view name() noexcept { return "camera_settings"; }
  static constexpr gneiss::guid guid() noexcept {
    return gneiss::guid::literal("9b432c67-043e-4af8-9614-244f92c57781");
  }
};

} // namespace gneiss

#endif
