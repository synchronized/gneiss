// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/vfs/asset_uri.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <memory>
#include <new>

namespace {

constexpr std::uint32_t prefab_resource_type = 4U;

void fail(gneiss::engine::resource::scene_diagnostic& diagnostic,
          gneiss::engine::core::result result, std::string_view path,
          std::string_view message) noexcept {
  diagnostic.result = result;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.result = gneiss::engine::core::result::out_of_memory;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

} // namespace

namespace gneiss::engine::resource {

gneiss::engine::core::result
prefab_asset_loader::install_prepared(std::string_view uri, prefab_description description,
                                      prefab_asset_ref& out_lease) noexcept {
  out_lease.reset();
  if (engine::resource::validate_uri(uri) != gneiss::engine::core::result::success ||
      description.objects.empty()) {
    return gneiss::engine::core::result::invalid_argument;
  }
  asset_identity identity;
  const auto identified = file_system_.identify(uri, prefab_resource_type, identity);
  if (identified != gneiss::engine::core::result::success) {
    return identified;
  }
  try {
    gneiss::engine::resource::asset_ref<const engine::resource::resource_cache::entry> entry;
    const auto result = cache_.load(
        uri, typeid(prefab_description),
        [&description](std::shared_ptr<const void>& resource) {
          resource = std::make_shared<prefab_description>(std::move(description));
          return gneiss::engine::core::result::success;
        },
        entry, identity.source, true);
    if (result == gneiss::engine::core::result::success) {
      out_lease = entry.alias(static_cast<const prefab_description*>(entry->resource.get()));
    }
    return result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result prefab_asset_loader::acquire(std::string_view uri,
                                                          prefab_asset_ref& output,
                                                          scene_diagnostic& diagnostic) noexcept {
  return load(uri, output, diagnostic, false);
}

gneiss::engine::core::result prefab_asset_loader::reload(std::string_view uri,
                                                         prefab_asset_ref& output,
                                                         scene_diagnostic& diagnostic) noexcept {
  return load(uri, output, diagnostic, true);
}

gneiss::engine::core::result prefab_asset_loader::load(std::string_view uri,
                                                       prefab_asset_ref& out_lease,
                                                       scene_diagnostic& out_diagnostic,
                                                       bool reload) noexcept {
  out_lease.reset();
  out_diagnostic = {};
  if (engine::resource::validate_uri(uri) != gneiss::engine::core::result::success) {
    fail(out_diagnostic, gneiss::engine::core::result::invalid_argument, "", "Prefab URI 无效");
    return out_diagnostic.result;
  }

  asset_identity identity;
  const auto identified = file_system_.identify(uri, prefab_resource_type, identity);
  if (identified != gneiss::engine::core::result::success)
    return identified;
  gneiss::engine::resource::asset_ref<const engine::resource::resource_cache::entry> entry;
  auto result = cache_.load(
      uri, typeid(prefab_description),
      [this, uri, &out_diagnostic](std::shared_ptr<const void>& out_resource) noexcept {
        try {
          auto description = std::make_shared<prefab_description>();
          const auto load_result =
              load_prefab_description(file_system_, uri, *description, out_diagnostic);
          if (load_result == gneiss::engine::core::result::success) {
            out_resource = std::move(description);
          }
          return load_result;
        } catch (const std::bad_alloc&) {
          fail(out_diagnostic, gneiss::engine::core::result::out_of_memory, "", "内存不足");
          return gneiss::engine::core::result::out_of_memory;
        } catch (...) {
          fail(out_diagnostic, gneiss::engine::core::result::internal, "",
               "Prefab Loader 内部错误");
          return gneiss::engine::core::result::internal;
        }
      },
      entry, identity.source, reload);
  if (result != gneiss::engine::core::result::success) {
    if (out_diagnostic.result == gneiss::engine::core::result::success) {
      fail(out_diagnostic, result, "", "无法获取 Prefab 资产");
    }
    return result;
  }
  out_lease = entry.alias(static_cast<const prefab_description*>(entry->resource.get()));
  return gneiss::engine::core::result::success;
}

} // namespace gneiss::engine::resource
