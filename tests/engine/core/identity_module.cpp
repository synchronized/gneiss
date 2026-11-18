// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/base/string_name.hpp>
#include <gneiss/engine/core/base/type_id.hpp>

#ifdef _WIN32
#define IDENTITY_EXPORT __declspec(dllexport)
#else
#define IDENTITY_EXPORT __attribute__((visibility("default")))
#endif

// 模块先驻留另一种类型，避免两份独立表恰好分配相同的首个编号而误通过。
IDENTITY_EXPORT gneiss::type_id module_integer_identity() {
  (void)gneiss::runtime_type_of<double>();
  return gneiss::runtime_type_of<int>();
}
IDENTITY_EXPORT gneiss::string_name module_string_identity() {
  return gneiss::string_name{"identity.test.module"};
}

IDENTITY_EXPORT gneiss::meta::registry_ref module_default_registry() {
  return gneiss::meta::default_registry();
}
