// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/type_id.hpp"
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <vector>
namespace gneiss::engine {
namespace {
// 标准容器替代 CAORS 内存/容器模块；映射节点保证名称地址不因后续注册改变。
struct identity_table {
  std::mutex mutex;
  std::map<std::string, type_id, std::less<>> by_name;
  std::vector<const std::string*> by_id;
};
identity_table& identities() {
  static identity_table table;
  return table;
}
}
type_id detail::intern_runtime_type(const char* name, std::size_t length) noexcept {
  if (!name || length == 0)
    std::terminate();
  auto& table = identities();
  const std::scoped_lock lock(table.mutex);
  const std::string_view key{name, length};
  if (const auto found = table.by_name.find(key); found != table.by_name.end())
    return found->second;
  if (table.by_id.size() == std::numeric_limits<std::uint64_t>::max())
    std::terminate();
  const type_id id{static_cast<std::uint64_t>(table.by_id.size()) + 1};
  const auto entry = table.by_name.emplace(std::string{key}, id).first;
  table.by_id.push_back(&entry->first);
  return id;
}
std::string_view runtime_type_name(type_id id) noexcept {
  auto& table = identities();
  const std::scoped_lock lock(table.mutex);
  return !id || id.value() > table.by_id.size()
             ? std::string_view{}
             : std::string_view{*table.by_id[static_cast<std::size_t>(id.value() - 1)]};
}
}
