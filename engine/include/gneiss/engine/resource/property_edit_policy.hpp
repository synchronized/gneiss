// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/property.hpp>
namespace gneiss {
/** 编辑与覆盖策略；不限制加载器恢复对象，不改变属性内存访问能力。 */
struct property_edit_policy {
  bool read_only = false;
};
[[nodiscard]] inline bool is_edit_read_only(const meta::property& property) noexcept {
  const auto* policy = property.extension<property_edit_policy>();
  return policy && policy->read_only;
}
}
