// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/input/action_map.hpp"
namespace gneiss::engine {
[[nodiscard]] result<action_map, meta::serialization_diagnostic>
parse_action_map(std::string_view json) noexcept;

/** 解码 input-map v1；限制 1 MiB，失败仅返回诊断，暂不提供编码能力。 */
[[nodiscard]] GNEISS_API result<action_map, meta::serialization_diagnostic>
    decode_action_map(std::span<const std::byte>) noexcept;
}
