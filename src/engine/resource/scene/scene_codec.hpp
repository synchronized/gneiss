// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "scene_description.hpp"
namespace gneiss::engine {
[[nodiscard]] result parse_scene_description(std::string_view json, scene_description& out_scene,
                                             scene_diagnostic& out_diagnostic) noexcept;

/** 输出当前 Schema 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] result serialize_scene_description(const scene_description& scene,
                                                 std::string& out_json) noexcept;

[[nodiscard]] GNEISS_API result decode_scene(std::span<const std::byte>, scene_description&,
                                             meta::serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result encode_scene(const scene_description&, std::vector<std::byte>&,
                                             meta::serialization_diagnostic&) noexcept;

}
