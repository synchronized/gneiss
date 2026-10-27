// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/resource/material/material_description.hpp"
namespace gneiss::engine {

/** 仅读写材质 JSON v5；旧版本返回 unsupported，不保留原文本排版。失败保持输出。 */
[[nodiscard]] GNEISS_API result
decode_material(std::span<const std::byte>, material_description&,
                gneiss::engine::meta::serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result
encode_material(const material_description&, std::vector<std::byte>&,
                gneiss::engine::meta::serialization_diagnostic&) noexcept;

}
