// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/reflection/serialization.hpp"
#include "engine/resource/types/prefab_description.hpp"
namespace gneiss::engine {
/** 复用既有版本与未知字段规则；失败保持输出。 */
[[nodiscard]] GNEISS_API result decode_scene(std::span<const std::byte>, scene_description&,
                                             serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result encode_scene(const scene_description&, std::vector<std::byte>&,
                                             serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result decode_prefab(std::span<const std::byte>, prefab_description&,
                                              serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result encode_prefab(const prefab_description&, std::vector<std::byte>&,
                                              serialization_diagnostic&) noexcept;
[[nodiscard]] GNEISS_API result register_scene_reflection(const registry_ref&) noexcept;
[[nodiscard]] GNEISS_API result register_prefab_reflection(const registry_ref&) noexcept;
}
