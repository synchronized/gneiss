// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/serialization_types.hpp"
#include "engine/resource/input/action_map.hpp"
namespace gneiss::engine {
[[nodiscard]] gneiss::engine::result
parse_action_map(std::string_view json, action_map& out_map,
                 gneiss::engine::meta::serialization_diagnostic* diagnostic = nullptr) noexcept;

/** 解码 input-map v1；限制 1 MiB，失败保持输出，暂不提供编码能力。 */
[[nodiscard]] GNEISS_API result
decode_action_map(std::span<const std::byte>, action_map&,
                  gneiss::engine::meta::serialization_diagnostic&) noexcept;

}
