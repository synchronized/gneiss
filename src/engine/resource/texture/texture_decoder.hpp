// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include "texture_data.hpp"
#include "texture_description.hpp"
namespace gneiss::engine::detail {
[[nodiscard]] result<texture_description, asset_diagnostic>
parse_texture(const std::vector<std::byte>& bytes);
[[nodiscard]] result<texture_data, asset_diagnostic>
decode_texture_payload(const texture_description& source, const std::vector<std::byte>& image_bytes,
                       std::size_t input_limit, std::size_t output_limit,
                       texture_prepare_profile profile) noexcept;
}
