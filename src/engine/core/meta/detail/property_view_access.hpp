// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include <functional>
namespace gneiss::engine::meta::detail {
/** 受约束属性的解码暂存；不向调用方暴露可写成员地址。 */
struct property_view_access {
  static bool requires_replacement(const mutable_instance& view) noexcept {
    return view.parent_ && !view.data_ && view.kind() != type_kind::scalar;
  }
  static GNEISS_API error
  replace(const mutable_instance&,
          const std::function<error(const mutable_instance&)>& initialize) noexcept;
};
}
