// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "action_map_codec.hpp"
#include "engine/core/meta/serialization.hpp"
namespace gneiss::engine {
using diagnostic = meta::serialization_diagnostic;
result<action_map, diagnostic> parse_action_map(std::string_view json) noexcept {
  return decode_action_map(std::as_bytes(std::span{json.data(), json.size()}));
}
result<action_map, diagnostic> decode_action_map(std::span<const std::byte> bytes) noexcept {
  return meta::deserialize<action_map>(bytes);
}
result<std::vector<std::byte>, diagnostic> encode_action_map(const action_map& object) noexcept {
  return meta::serialize(object);
}
}
