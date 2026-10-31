// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "material_codec.hpp"
#include "engine/core/meta/serialization.hpp"
namespace gneiss::engine {
using diagnostic = meta::serialization_diagnostic;
result<material_description, diagnostic>
decode_material(std::span<const std::byte> bytes) noexcept {
  return meta::deserialize<material_description>(bytes);
}
result<std::vector<std::byte>, diagnostic>
encode_material(const material_description& object) noexcept {
  return meta::serialize(object);
}
}
