// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/result.hpp"
#include "scene_description.hpp"
namespace gneiss::engine {
[[nodiscard]] result<scene_description, scene_diagnostic>
parse_scene_description(std::string_view json) noexcept;

/** 输出当前 Schema 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] result<std::string>
serialize_scene_description(const scene_description& scene) noexcept;

[[nodiscard]] GNEISS_API result<scene_description, meta::serialization_diagnostic>
    decode_scene(std::span<const std::byte>) noexcept;
[[nodiscard]] GNEISS_API result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_scene(const scene_description&) noexcept;
}
