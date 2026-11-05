// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ACTION_MAP_HPP_
#define GNEISS_ASSET_ACTION_MAP_HPP_

#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine::serialization {
struct diagnostic;
}

namespace gneiss::engine {

struct action_binding final {
  inline static constexpr std::string_view type_name = "action_binding";
  inline static constexpr guid type_guid = guid::literal("97050000-0000-0000-0000-000000000000");
  std::uint32_t physical_key{};
  float scale{};
};

struct action_definition final {
  inline static constexpr std::string_view type_name = "action_definition";
  inline static constexpr guid type_guid = guid::literal("97060000-0000-0000-0000-000000000000");
  std::string name;
  std::vector<action_binding> bindings;
};

/** 输入文件及反射解码统一上限。 */
inline constexpr std::size_t maximum_action_map_bytes = 1024U * 1024U;

/** 可共享的只读描述；实时输入状态由 InputService 单独管理。 */
struct action_map final {
  inline static constexpr std::string_view type_name = "action_map";
  inline static constexpr engine::guid type_guid{
      engine::guid::literal("2c8319d2-673f-4a51-9380-7ac502e694b1")};
  inline static constexpr std::size_t maximum_source_bytes = maximum_action_map_bytes;
  std::vector<action_definition> actions;
};

/** 原生构造与 JSON 加载共享语义约束；不改变输入映射。 */
[[nodiscard]] error validate_action_map(const action_map& map) noexcept;

} // namespace gneiss::engine

#endif
