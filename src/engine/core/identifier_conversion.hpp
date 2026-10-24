// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/scene_types.hpp>
#include <utility>
namespace gneiss::engine {
/** 整数存储/ABI 输出边界：显式复制强类型标识，不重解释引用，不建立句柄表。
 * 无论成功或失败均保留被调用接口的输出语义（清零或保持原值）。 */
template <typename Identifier, typename Function>
  requires(std::same_as<Identifier, entity_id> || std::same_as<Identifier, scene_node_id>)
auto write_identifier(std::uint64_t& destination, Function&& operation) {
  Identifier value{destination};
  const auto result = std::forward<Function>(operation)(value);
  destination = value.get();
  return result;
}
}
