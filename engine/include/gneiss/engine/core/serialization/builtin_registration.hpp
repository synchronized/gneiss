// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/registry.hpp>

namespace gneiss::serialization {
/** 注册基础值、数学类型及其持久化策略；不冻结，重复注册按普通注册规则失败。 */
GNEISS_API error register_builtin_types(const meta::registry_ref&);
inline error register_builtin_types() {
  auto registry = meta::default_registry();
  return registry ? register_builtin_types(*registry) : registry.error();
}
}
