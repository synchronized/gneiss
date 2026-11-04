// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <span>
namespace gneiss::engine::detail {
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
