// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "asset_entry.hpp"
#include "engine/resource/asset_diagnostic.hpp"
#include <unordered_set>
namespace gneiss::detail {
/** 图仅弱引用记录；内容拥有关系与本次加载的候选边分别保留。
 * 提交依赖前检查，拒绝等待环及将来装配产生的强拥有环。限所属线程。 */
inline asset_diagnostic link_dependency(const std::shared_ptr<asset_entry>& owner,
                                        const std::shared_ptr<asset_entry>& target,
                                        std::string_view path) {
  std::unordered_set<const asset_entry*> visited;
  std::vector<std::shared_ptr<asset_entry>> chain;
  const auto reaches_owner = [&](auto&& self, const std::shared_ptr<asset_entry>& node) -> bool {
    chain.push_back(node);
    if (node == owner)
      return true;
    if (!visited.insert(node.get()).second) {
      chain.pop_back();
      return false;
    }
    const auto follow = [&](const auto& edges) {
      for (const auto& edge : edges)
        if (auto next = edge.target.lock(); next && self(self, next))
          return true;
      return false;
    };
    if ((node->loading && follow(node->pending_dependencies)) ||
        (node->cached && !node->cached->expired() && follow(node->owned_dependencies)))
      return true;
    chain.pop_back();
    return false;
  };
  if (reaches_owner(reaches_owner, target)) {
    std::string message = "资产依赖形成循环: " + std::get<1>(owner->identity);
    for (const auto& node : chain)
      message += " -> " + std::get<1>(node->identity);
    return {error::invalid_state, 0, std::string(path), std::move(message)};
  }
  for (const auto& edge : owner->pending_dependencies)
    if (edge.target.lock() == target && edge.path == path)
      return {};
  owner->pending_dependencies.push_back({target, std::string(path)});
  return {};
}
}
