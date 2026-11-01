// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include "engine/resource/mesh/mesh_data.hpp"
namespace gneiss::engine::detail {
[[nodiscard]] result<mesh_data, asset_diagnostic> parse_mesh(const std::vector<std::byte>& bytes);

[[nodiscard]] result<mesh_data, asset_diagnostic>
parse_binary_mesh(const std::vector<std::byte>& bytes);
}
