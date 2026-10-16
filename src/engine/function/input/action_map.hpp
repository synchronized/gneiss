// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_ACTION_MAP_HPP_
#define GNEISS_INPUT_ACTION_MAP_HPP_

#include <gneiss/engine/core/result.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::input_internal {

/** 只在所属输入服务及映射代次内有效；零值无效。 */
struct action_id final {
  std::uint64_t value{};
  friend bool operator==(action_id, action_id) = default;
};

struct action_state final {
  bool pressed{};
  bool held{};
  bool released{};
  float value{};
};

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

/** 原生构造与 JSON 加载共享语义约束；不改变输入映射。 */
[[nodiscard]] result validate_action_map(const action_map& map) noexcept;

[[nodiscard]] result parse_action_map(std::string_view json, action_map& out_map) noexcept;
[[nodiscard]] result load_action_map(const asset_internal::virtual_file_system& file_system,
                                     std::string_view uri, action_map& out_map) noexcept;

} // namespace gneiss::input_internal

#endif
