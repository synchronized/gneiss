// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/meta/type.hpp>

namespace gneiss::meta {

/** 只读类型视图；持有注册表，不持有对象。借出的元数据不得超过本视图寿命。 */
namespace detail {
GNEISS_META_API meta::type resolve_type(const registry_ref&, const type_ops&);
}

} // namespace gneiss::meta
