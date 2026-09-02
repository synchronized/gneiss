// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/prefab_asset_loader.h"

#include "asset/asset_uri.h"
#include "asset/virtual_file_system.h"

#include <memory>
#include <new>

namespace {

constexpr std::uint32_t prefab_resource_type = 4U;

void fail(gneiss::scene_internal::scene_diagnostic& diagnostic, gneiss_result result,
          std::string_view path, std::string_view message) noexcept {
  diagnostic.result = result;
  try {
    diagnostic.path = path;
    diagnostic.message = message;
  } catch (...) {
    diagnostic.result = GNEISS_ERROR_OUT_OF_MEMORY;
    diagnostic.path.clear();
    diagnostic.message.clear();
  }
}

} // namespace

namespace gneiss::scene_internal {

const prefab_description* prefab_asset_lease::get() const noexcept {
  return entry_ ? static_cast<const prefab_description*>(entry_->resource.get()) : nullptr;
}

gneiss_result prefab_asset_loader::acquire(std::string_view uri, prefab_asset_lease& out_lease,
                                           scene_diagnostic& out_diagnostic) noexcept {
  out_lease.entry_.reset();
  out_diagnostic = {};
  if (asset_internal::validate_uri(uri) != GNEISS_SUCCESS) {
    fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "Prefab URI 无效");
    return out_diagnostic.result;
  }

  std::shared_ptr<const asset_internal::resource_cache::entry> entry;
  auto result = cache_.acquire(
      uri, prefab_resource_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& out_resource) noexcept {
        try {
          auto description = std::make_shared<prefab_description>();
          const auto load_result =
              load_prefab_description(file_system_, uri, *description, out_diagnostic);
          if (load_result == GNEISS_SUCCESS) {
            out_resource = std::move(description);
          }
          return load_result;
        } catch (const std::bad_alloc&) {
          fail(out_diagnostic, GNEISS_ERROR_OUT_OF_MEMORY, "", "内存不足");
          return GNEISS_ERROR_OUT_OF_MEMORY;
        } catch (...) {
          fail(out_diagnostic, GNEISS_ERROR_INTERNAL, "", "Prefab Loader 内部错误");
          return GNEISS_ERROR_INTERNAL;
        }
      },
      entry);
  if (result != GNEISS_SUCCESS) {
    if (out_diagnostic.result == GNEISS_SUCCESS) {
      fail(out_diagnostic, result, "", "无法获取 Prefab 资产");
    }
    return result;
  }
  out_lease.entry_ = std::move(entry);
  return GNEISS_SUCCESS;
}

} // namespace gneiss::scene_internal
