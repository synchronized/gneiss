// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/instance.hpp"
#include "panels/property_path.hpp"
#include <optional>
namespace gneiss::editor {
bool draw_property_value(const char* name, engine::value&,
                         std::span<const engine::meta::enum_entry>, std::uint64_t& serial,
                         engine::error&);
std::optional<property_edit>
draw_reflected_properties(const engine::meta::instance&, const char* name, std::uint64_t& serial,
                          engine::error&, std::span<const engine::meta::property_id> readonly = {});
}
