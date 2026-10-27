// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/asset_diagnostic.hpp"
#include "texture_data.hpp"
#include "texture_description.hpp"
namespace gneiss::engine::detail {
[[nodiscard]] result parse_texture(const std::vector<std::byte>& bytes, texture_description& output,
                                   asset_diagnostic& diagnostic);
[[nodiscard]] result decode_texture_payload(const texture_description& source,
                                            const std::vector<std::byte>& image_bytes,
                                            prepared_texture_data& output,
                                            asset_diagnostic& diagnostic, std::size_t input_limit,
                                            std::size_t output_limit,
                                            texture_prepare_profile profile) noexcept;
}
