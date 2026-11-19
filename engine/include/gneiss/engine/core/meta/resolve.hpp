// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/export.hpp>
#include <gneiss/engine/core/meta/ops/type_ops.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/meta/type.hpp>

#include <type_traits>

namespace gneiss::meta {
/** 查询冻结注册表；未发布或未找到返回空句柄，基础值不需重复登记。 */
GNEISS_META_API meta::type resolve(const registry_ref&, gneiss::type_id);
GNEISS_META_API meta::type resolve(const registry_ref&, gneiss::guid);
/** 按 C++ 类型解析原生能力和已发布注册信息；不隐式注册或修改注册表。 */
template <class T> meta::type resolve(const registry_ref& registry) {
  return detail::resolve_type(registry, detail::ops_of<std::remove_cvref_t<T>>());
}

template <class T> meta::type resolve() { return resolve<T>(default_registry()); }
inline meta::type resolve(gneiss::type_id id) { return resolve(default_registry(), id); }
inline meta::type resolve(gneiss::guid id) { return resolve(default_registry(), id); }
} // namespace gneiss::meta
