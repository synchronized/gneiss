// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <functional>
#include <gneiss/engine/core/meta/type.hpp>

namespace gneiss::meta::cap {
/** 类型能力视图，持有类型上下文，不持有实例；转换回调只允许同步借用。 */
class representation {
  meta::type type_;

public:
  representation() = default;
  explicit GNEISS_API representation(meta::type target);
  [[nodiscard]] explicit operator bool() const noexcept { return bool(type_); }
  /** 要求有效能力与回调；源实例类型/注册表不匹配返回 invalid_argument。 */
  [[nodiscard]] GNEISS_API error
  encode(const const_instance& source,
         const std::function<error(const const_instance&)>& consume) const;
  /** 先读表示，再访问候选；不自动写入目标，候选只在回调内有效。 */
  [[nodiscard]] GNEISS_API error decode(const std::function<error(const instance&)>& read,
                                        const std::function<error(const instance&)>& consume) const;
};
}
