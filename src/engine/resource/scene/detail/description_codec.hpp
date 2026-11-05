// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <span>
struct yyjson_val;
struct yyjson_mut_val;
struct yyjson_mut_doc;
namespace gneiss::engine::detail {
/** 内部 DOM 适配；成功返回非空、无重复类型的组件列表，失败不发布部分结果。 */
GNEISS_API result<component_descriptions, meta::serialization_diagnostic>
read_components(yyjson_val*) noexcept;
GNEISS_API result<yyjson_mut_val*, meta::serialization_diagnostic>
write_components(yyjson_mut_doc*, const component_descriptions&) noexcept;
}
