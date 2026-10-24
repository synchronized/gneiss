// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_TYPE_IDENTITY_HPP_
#define GNEISS_ENGINE_TYPE_IDENTITY_HPP_

#include "engine/core/traits/type_name.hpp"
#include "engine/core/type_guid.hpp"

#include <string_view>
#include <type_traits>

namespace gneiss::engine {

/**
 * @brief C++ 类型的静态名称和稳定 GUID 定制点。
 *
 * 默认名称复用 `type_name<T>`，默认 GUID 为空。模块可以对第二模板参数做受约束偏特化，
 * 用户类型也可以直接特化本 traits。
 */
template <typename T, typename = void> struct type_identity {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;

  [[nodiscard]] static std::string_view name() noexcept {
    if constexpr (has_type_name_v<type>) {
      return type_name<type>::value;
    } else {
      return {};
    }
  }

  [[nodiscard]] static constexpr type_guid guid() noexcept { return type_guid::nil(); }
};

} // namespace gneiss::engine

#endif
