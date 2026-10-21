// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/backend/caors/caors_action_map.hpp"
namespace gneiss::engine::resource {
std::unique_ptr<action_map_loader>
make_action_map_loader(caors_internal::file_system_adapter& files,
                       std::function<result(std::string_view, std::span<const std::byte>)> verify) {
  return std::make_unique<caors_internal::bounded_document_loader<action_map_asset>>(
      files,
      [](auto bytes, action_map_asset& value) {
        return parse_action_map({reinterpret_cast<const char*>(bytes.data()), bytes.size()},
                                value.map);
      },
      std::move(verify), maximum_action_map_bytes);
}
} // namespace gneiss::engine::resource
