// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "prefab_description.hpp"
namespace gneiss::engine {
[[nodiscard]] result parse_prefab_description(std::string_view json, prefab_description& out_prefab,
                                              scene_diagnostic& out_diagnostic) noexcept;

/** 输出 Prefab v1 作者 JSON；保留受支持文档中的未知字段。 */
[[nodiscard]] result serialize_prefab_description(const prefab_description& prefab,
                                                  std::string& out_json) noexcept;

[[nodiscard]] GNEISS_API result decode_prefab(std::span<const std::byte>, prefab_description&,
                                              meta::serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result encode_prefab(const prefab_description&, std::vector<std::byte>&,
                                              meta::serialization_diagnostic&) noexcept;

}
