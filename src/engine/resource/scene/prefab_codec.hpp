// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/result.hpp"
#include "prefab_description.hpp"
namespace gneiss::engine {
[[nodiscard]] result<prefab_description, scene_diagnostic>
parse_prefab_description(std::string_view json) noexcept;

/** 输出 Prefab v1 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] result<std::string>
serialize_prefab_description(const prefab_description& prefab) noexcept;

[[nodiscard]] GNEISS_API result<prefab_description, meta::serialization_diagnostic>
    decode_prefab(std::span<const std::byte>) noexcept;
[[nodiscard]] GNEISS_API result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_prefab(const prefab_description&) noexcept;
}
