// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/scene/scene_description.hpp"
#include <span>
namespace gneiss::engine::detail {
/** 覆盖协议的内部编解码入口，供内置类型注册使用。 */
result<std::vector<std::byte>, meta::serialization_diagnostic>
encode_property_override(const prefab_property_override&);
result<prefab_property_override, meta::serialization_diagnostic>
    decode_property_override(std::span<const std::byte>);
}
