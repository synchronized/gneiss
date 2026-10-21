// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_ACTION_MAP_HPP_
#define GNEISS_ASSET_ACTION_MAP_HPP_

#include <gneiss/engine/core/result.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine::resource {
class virtual_file_system;
}

namespace gneiss::engine::resource {

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
/** 输入文档统一上限；同步读取、CAORS 桥和 Loader 共用。 */
inline constexpr std::size_t maximum_action_map_bytes = 1024U * 1024U;
/** 重新读取并比较完整文档；不修改输入服务，来源变化返回 invalid_state。 */
[[nodiscard]] result verify_action_map_source(const engine::resource::virtual_file_system& files,
                                              std::string_view uri,
                                              std::span<const std::byte> original) noexcept;
/** 有界读取、解析、复验成功后才替换输出；调用线程执行，不推进任何执行器。 */
[[nodiscard]] result load_action_map(const engine::resource::virtual_file_system& file_system,
                                     std::string_view uri, action_map& out_map) noexcept;

} // namespace gneiss::engine::resource

#endif
