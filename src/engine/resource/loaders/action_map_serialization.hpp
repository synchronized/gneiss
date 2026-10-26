// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization.hpp"
#include "engine/resource/types/action_map.hpp"
namespace gneiss::engine {
/** 解码 input-map v1；限制 1 MiB，失败保持输出，暂不提供编码能力。 */
[[nodiscard]] GNEISS_API result
decode_action_map(std::span<const std::byte>, action_map&,
                  gneiss::engine::meta::serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result
register_action_map_reflection(const gneiss::engine::meta::registry_ref&) noexcept;
}
