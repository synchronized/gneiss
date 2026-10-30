// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/property.hpp"
#include <variant>
namespace gneiss::editor {
using property_step = std::variant<engine::meta::property_id, std::size_t>;
using property_path = std::vector<property_step>;
}
