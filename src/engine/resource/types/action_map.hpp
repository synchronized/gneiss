// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ACTION_MAP_HPP_
#define GNEISS_ASSET_ACTION_MAP_HPP_

#include "engine/core/guid.hpp"
#include "engine/core/result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {
struct serialization_diagnostic;
}

namespace gneiss::engine {

struct action_binding final {
  std::uint32_t physical_key{};
  float scale{};
};

struct action_definition final {
  std::string name;
  std::vector<action_binding> bindings;
};

/** 输入文件及反射解码统一上限。 */
inline constexpr std::size_t maximum_action_map_bytes = 1024U * 1024U;

/** 可共享的只读描述；实时输入状态由 InputService 单独管理。 */
struct action_map final {
  inline static constexpr std::string_view type_name = "action_map";
  inline static constexpr engine::guid type_guid{
      engine::guid::from_bytes({0x2c, 0x83, 0x19, 0xd2, 0x67, 0x3f, 0x4a, 0x51, 0x93, 0x80, 0x7a,
                                0xc5, 0x02, 0xe6, 0x94, 0xb1})};
  inline static constexpr std::size_t maximum_source_bytes = maximum_action_map_bytes;
  std::vector<action_definition> actions;
};

/** 原生构造与 JSON 加载共享语义约束；不改变输入映射。 */
[[nodiscard]] gneiss::engine::result validate_action_map(const action_map& map) noexcept;

[[nodiscard]] gneiss::engine::result
parse_action_map(std::string_view json, action_map& out_map,
                 serialization_diagnostic* diagnostic = nullptr) noexcept;

} // namespace gneiss::engine

#endif
