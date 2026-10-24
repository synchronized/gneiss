// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/types/action_map.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include <utility>

namespace gneiss::engine {
gneiss::engine::result load_action_map(const gneiss::engine::virtual_file_system& file_system,
                                       std::string_view uri, action_map& out_map) noexcept {
  std::vector<std::byte> bytes;
  const auto operation = file_system.read_bounded(uri, maximum_action_map_bytes, bytes);
  if (operation.failed()) {
    return operation;
  }
  action_map pending;
  const auto parsed =
      parse_action_map({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, pending);
  if (parsed != gneiss::engine::result::success)
    return parsed;
  out_map = std::move(pending);
  return gneiss::engine::result::success;
}

} // namespace gneiss::engine
