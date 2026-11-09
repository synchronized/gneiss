// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/reference_view.hpp"
namespace gneiss::engine::meta::detail {
/** 对象图编解码专用：显式保活及回填。调用方保证 owner 实际类型与 id 匹配。 */
struct reference_view_access {
  static GNEISS_API result<engine::type_id> dynamic_type(const const_instance& view) noexcept;
  static GNEISS_API result<ref<const void>> owner(const const_instance& view) noexcept;
  static GNEISS_API error assign(const instance& view, engine::type_id id,
                                 const ref<void>& owner) noexcept;
  static GNEISS_API error reset(const instance& view) noexcept;
};
}
