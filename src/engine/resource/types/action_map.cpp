// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/types/action_map.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include <algorithm>

namespace gneiss::engine::resource {
result verify_action_map_source(const engine::resource::virtual_file_system& files,
                                std::string_view uri,
                                std::span<const std::byte> original) noexcept {
  if (original.size() > maximum_action_map_bytes)
    return result::invalid_argument;
  std::vector<std::byte> current;
  const auto read = from_native(files.read_bounded(uri, maximum_action_map_bytes, current));
  if (read != result::success)
    return read;
  return std::ranges::equal(current, original) ? result::success : result::invalid_state;
}

result load_action_map(const engine::resource::virtual_file_system& file_system,
                       std::string_view uri, action_map& out_map) noexcept {
  std::vector<std::byte> bytes;
  const auto operation =
      from_native(file_system.read_bounded(uri, maximum_action_map_bytes, bytes));
  if (operation.failed()) {
    return operation;
  }
  action_map pending;
  const auto parsed =
      parse_action_map({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, pending);
  if (parsed != result::success)
    return parsed;
  const auto verified = verify_action_map_source(file_system, uri, bytes);
  if (verified != result::success)
    return verified;
  out_map = std::move(pending);
  return result::success;
}

} // namespace gneiss::engine::resource
