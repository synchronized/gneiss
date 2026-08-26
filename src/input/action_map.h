// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_ACTION_MAP_H_
#define GNEISS_INPUT_ACTION_MAP_H_

#include <gneiss/core/result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::input_internal {

struct action_binding final {
  std::uint32_t physical_key{};
  float scale{};
};

struct action_definition final {
  std::string name;
  std::vector<action_binding> bindings;
};

struct action_map final {
  std::vector<action_definition> actions;
};

[[nodiscard]] gneiss_result parse_action_map(std::string_view json, action_map& out_map) noexcept;
[[nodiscard]] gneiss_result load_action_map(const asset_internal::virtual_file_system& file_system,
                                            std::string_view uri, action_map& out_map) noexcept;

} // namespace gneiss::input_internal

#endif
