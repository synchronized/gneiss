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
// 编解码期间的临时结果，不建立第二套运行时对象模型。
struct decoded_object_document {
  guid uuid;
  std::optional<guid> root_uuid;
  std::vector<object_description> objects;
};
result<decoded_object_document, scene_diagnostic> parse_object_document(std::string_view json,
                                                                        bool prefab) noexcept;
result<std::string> serialize_object_document(guid uuid, std::optional<guid> root_uuid,
                                              std::span<const object_description> objects) noexcept;
}
