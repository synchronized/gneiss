// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
struct yyjson_val;
struct yyjson_mut_val;
struct yyjson_mut_doc;
namespace gneiss::engine::meta::detail {
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_instance(yyjson_mut_doc*, const registry_ref&, const instance&) noexcept;
GNEISS_API serialization_diagnostic read_instance(yyjson_val*, const registry_ref&,
                                                  const mutable_instance&) noexcept;
/** 使用视图自身的注册表。 */
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_instance(yyjson_mut_doc*, const instance&) noexcept;
GNEISS_API serialization_diagnostic read_instance(yyjson_val*, const mutable_instance&) noexcept;
/** 内部 JSON 适配：类型标签保留 value 的精确类别，标量规则与反射属性共用。 */
GNEISS_API result<value, serialization_diagnostic> read_tagged_value(yyjson_val*) noexcept;
GNEISS_API result<yyjson_mut_val*, serialization_diagnostic>
write_tagged_value(yyjson_mut_doc*, const value&) noexcept;
}
