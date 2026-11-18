// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/result.hpp>
#include <gneiss/engine/resource/asset_diagnostic.hpp>
#include <gneiss/engine/resource/texture/texture_data.hpp>
#include <gneiss/engine/resource/texture/texture_description.hpp>
namespace gneiss::detail {
[[nodiscard]] GNEISS_API result<std::vector<std::byte>, asset_diagnostic>
encode_texture(const texture_description& description);
[[nodiscard]] GNEISS_API result<texture_description, asset_diagnostic>
parse_texture(const std::vector<std::byte>& bytes);
[[nodiscard]] result<texture_data, asset_diagnostic>
decode_texture_payload(const texture_description& source, const std::vector<std::byte>& image_bytes,
                       texture_prepare_profile profile);
}
