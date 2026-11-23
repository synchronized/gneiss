// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta::detail {
/** Meta 与序列化的私有协作协议；管理临时存储与候选提交，业务层仅表达读写意图。 */
struct instance_access {
  static GNEISS_META_API error read(const const_instance&,
                                    const std::function<error(const const_instance&)>&);
  static bool constrained(const instance& target) noexcept {
    return target.parent_ && !target.writable_;
  }
  static GNEISS_META_API error replace(const instance&,
                                       const std::function<error(const instance&)>&);
  static GNEISS_META_API error edit(const instance&, const std::function<error(const instance&)>&);
};
}
