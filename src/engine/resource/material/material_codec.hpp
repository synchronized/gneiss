// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/material/material_description.hpp"
namespace gneiss::engine {

/** 仅读写材质 JSON v5；旧版本返回 unsupported，不保留原文本排版。失败仅返回诊断。 */
[[nodiscard]] GNEISS_API result<material_description, meta::serialization_diagnostic>
    decode_material(std::span<const std::byte>) noexcept;
[[nodiscard]] GNEISS_API result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_material(const material_description&) noexcept;
}
