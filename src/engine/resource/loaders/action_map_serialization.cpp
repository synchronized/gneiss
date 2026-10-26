// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "action_map_serialization.hpp"
#include "engine/core/meta/type_builder.hpp"
namespace gneiss::engine {
result decode_action_map(std::span<const std::byte> bytes, action_map& output,
                         serialization_diagnostic& diagnostic) noexcept {
  return parse_action_map({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, output,
                          &diagnostic);
}
result register_action_map_reflection(const registry_ref& registry) noexcept {
  return type_builder<action_map>{registry}.serialization<nullptr, &decode_action_map>().commit();
}
}
