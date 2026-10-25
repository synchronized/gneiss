// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_manager.hpp"

#include "engine/resource/vfs/asset_uri.hpp"

#include <memory>
#include <new>

namespace gneiss::engine {

gneiss::engine::result prefab_asset_loader::install_prepared(std::string_view uri,
                                                             prefab_description description,
                                                             prefab_asset_ref& out_lease) noexcept {
  if (description.objects.empty())
    return result::invalid_argument;
  try {
    return assets_.store<prefab_description>(
        uri, std::make_shared<const prefab_description>(std::move(description)), out_lease);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

gneiss::engine::result prefab_asset_loader::acquire(std::string_view uri, prefab_asset_ref& output,
                                                    scene_diagnostic& diagnostic) noexcept {
  return load(uri, output, diagnostic, false);
}

gneiss::engine::result prefab_asset_loader::reload(std::string_view uri, prefab_asset_ref& output,
                                                   scene_diagnostic& diagnostic) noexcept {
  return load(uri, output, diagnostic, true);
}

gneiss::engine::result prefab_asset_loader::load(std::string_view uri, prefab_asset_ref& out_lease,
                                                 scene_diagnostic& out_diagnostic,
                                                 bool reload) noexcept {
  serialization_diagnostic decoded;
  const auto status = assets_.load_sync(uri, out_lease, reload, &decoded);
  out_diagnostic = {status, decoded.byte_offset, std::move(decoded.path),
                    std::move(decoded.message)};
  if (status.failed() && out_diagnostic.message.empty()) {
    try {
      out_diagnostic.message = "无法获取 Prefab 资产";
    } catch (...) {
    }
  }
  return status;
}

} // namespace gneiss::engine
