// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/type_id.hpp"
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
namespace gneiss::engine {
namespace {
// 编译器类型名仅作为内部去重键，不对调用者提供名称查询。
struct identity_table {
  std::mutex mutex;
  std::map<std::string, type_id, std::less<>> by_name;
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
  if (table.by_name.size() == std::numeric_limits<std::uint64_t>::max())
    std::terminate();
  const type_id id{static_cast<std::uint64_t>(table.by_name.size()) + 1};
  table.by_name.emplace(std::string{key}, id);
  return id;
}
}
