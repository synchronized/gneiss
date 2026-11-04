// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_TYPE_IDENTITY_HPP_
#define GNEISS_ENGINE_TYPE_IDENTITY_HPP_

#include "engine/core/guid.hpp"
#include "engine/core/math/camera.hpp"
#include "engine/core/math/types.hpp"
#include "engine/core/traits/type_name.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace gneiss::engine {

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

  [[nodiscard]] static constexpr engine::guid guid() noexcept {
    if constexpr (requires { engine::guid{type::type_guid}; }) {
      return engine::guid{type::type_guid};
    } else {
      return engine::guid::nil();
    }
  }
};

template <class T>
  requires(!std::is_same_v<T, std::remove_cvref_t<T>>)
struct type_identity<T> : type_identity<std::remove_cvref_t<T>> {};

/** 当前内建反射字段的持久化身份，沿用既有 GUID。 */
template <> struct type_identity<math::vector3> {
  static constexpr std::string_view name() noexcept { return "vec3"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x90, 0xbc, 0xc8, 0x13, 0xa5, 0x3c, 0x41, 0x96, 0xaa, 0x35,
                                     0x93, 0xf8, 0x94, 0x6e, 0x82, 0xcb});
  }
};
template <> struct type_identity<math::vector2> {
  static constexpr std::string_view name() noexcept { return "vec2"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x1e, 0x1a, 0x6e, 0xaf, 0xf3, 0x85, 0x40, 0x9c, 0xb3, 0x65,
                                     0xd9, 0x8b, 0xe4, 0x5f, 0x47, 0xd4});
  }
};
template <> struct type_identity<math::vector4> {
  static constexpr std::string_view name() noexcept { return "vec4"; }
  static constexpr engine::guid guid() noexcept { return engine::guid::from_bytes({0x97, 1}); }
};
template <> struct type_identity<math::quaternion> {
  static constexpr std::string_view name() noexcept { return "quaternion"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x21, 0xc1, 0x11, 0xbd, 0xd7, 0x54, 0x44, 0xf0, 0x89, 0xc1,
                                     0xf7, 0xcc, 0x27, 0x75, 0xd9, 0x1e});
  }
};
template <> struct type_identity<float> {
  static constexpr std::string_view name() noexcept { return "float"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0xa2, 0x7e, 0x99, 0x02, 0xf3, 0xcb, 0x48, 0x45, 0xa7, 0xe8,
                                     0x32, 0x17, 0xf9, 0x75, 0x9f, 0xc5});
  }
};
template <> struct type_identity<bool> {
  static constexpr std::string_view name() noexcept { return "bool"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0xc2, 0xaf, 0x2a, 0xd1, 0xa6, 0x25, 0x48, 0x42, 0xb4, 0xdd,
                                     0x74, 0xe9, 0x77, 0x06, 0x7e, 0x35});
  }
};

template <> struct type_identity<std::int64_t> {
  static constexpr std::string_view name() noexcept { return "int64"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x81, 0x3f, 0x73, 0xd9, 0x20, 0x5e, 0x42, 0xf2, 0x93, 0x3b,
                                     0xc3, 0xef, 0xb0, 0x90, 0xca, 0xf1});
  }
};
template <> struct type_identity<std::uint64_t> {
  static constexpr std::string_view name() noexcept { return "uint64"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x2e, 0xc2, 0x37, 0xdb, 0x7d, 0xe3, 0x41, 0xdc, 0xa0, 0x77,
                                     0x1e, 0x5d, 0xac, 0x89, 0x18, 0x1c});
  }
};
template <> struct type_identity<double> {
  static constexpr std::string_view name() noexcept { return "double"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0xbb, 0x42, 0xb8, 0x6e, 0x4a, 0x80, 0x46, 0x95, 0x99, 0x78,
                                     0x14, 0xd6, 0x2c, 0xee, 0xb2, 0x17});
  }
};
template <> struct type_identity<std::string> {
  static constexpr std::string_view name() noexcept { return "string"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x60, 0x54, 0x29, 0x78, 0xc4, 0x90, 0x4a, 0xb5, 0x83, 0x50,
                                     0xa9, 0x01, 0xec, 0xc9, 0x8e, 0xa8});
  }
};
template <> struct type_identity<engine::guid> {
  static constexpr std::string_view name() noexcept { return "guid"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x5f, 0x7a, 0xd2, 0x9c, 0x1a, 0xa6, 0x43, 0x4f, 0x9e, 0xc8,
                                     0x80, 0xec, 0xdf, 0x59, 0xe1, 0x82});
  }
};

template <> struct type_identity<math::camera_settings> {
  static constexpr std::string_view name() noexcept { return "camera_settings"; }
  static constexpr engine::guid guid() noexcept {
    return engine::guid::from_bytes({0x9b, 0x43, 0x2c, 0x67, 0x04, 0x3e, 0x4a, 0xf8, 0x96, 0x14,
                                     0x24, 0x4f, 0x92, 0xc5, 0x77, 0x81});
  }
};

} // namespace gneiss::engine

#endif
