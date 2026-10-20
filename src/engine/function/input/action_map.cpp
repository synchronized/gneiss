// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/input/action_map.hpp"
#include "engine/asset/virtual_file_system.hpp"

namespace gneiss::input_internal {
result load_action_map(const asset_internal::virtual_file_system& file_system, std::string_view uri,
                       action_map& out_map) noexcept {
  std::vector<std::byte> bytes;
  const auto operation = from_native(file_system.read(uri, bytes));
  if (operation.failed()) {
    return operation;
  }
  return parse_action_map(
      {reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(bytes.size())},
      out_map);
}

} // namespace gneiss::input_internal
