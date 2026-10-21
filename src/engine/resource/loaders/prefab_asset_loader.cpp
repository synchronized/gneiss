// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/loaders/prefab_asset_loader.hpp"

#include "engine/core/sha256.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <bit>
#include <memory>
#include <new>

namespace {

constexpr std::uint32_t prefab_resource_type = 4U;

// 按字段编码，不摘要结构体填充或机器字节序；新增描述字段时须同时更新版本编码。
std::string description_version(const gneiss::scene_internal::prefab_description& description) {
  gneiss::core::sha256_builder hash;
  const auto number = [&](std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (auto& byte : bytes) {
      byte = static_cast<std::byte>(value & 255U);
      value >>= 8U;
    }
    hash.update(bytes);
  };
  const auto text = [&](std::string_view value) {
    number(value.size());
    hash.update(std::as_bytes(std::span(value.data(), value.size())));
  };
  const auto real = [&](float value) { number(std::bit_cast<std::uint32_t>(value)); };
  number(description.source_schema_version);
  text(description.uuid);
  text(description.author_json);
  number(description.dependencies.size());
  for (const auto& dependency : description.dependencies) {
    text(dependency);
  }
  number(description.objects.size());
  for (const auto& object : description.objects) {
    text(object.uuid);
    text(object.name);
    number(static_cast<std::uint64_t>(object.parent_uuid.has_value()));
    if (object.parent_uuid.has_value()) {
      text(*object.parent_uuid);
    }
    for (const auto value : object.translation) {
      real(value);
    }
    for (const auto value : object.rotation) {
      real(value);
    }
    for (const auto value : object.scale) {
      real(value);
    }
    const auto camera_value = object.camera;
    number(static_cast<std::uint64_t>(camera_value.has_value()));
    if (camera_value.has_value()) {
      const auto& camera = *camera_value;
      real(camera.vertical_field_of_view_radians);
      real(camera.near_plane);
      real(camera.far_plane);
      number(static_cast<std::uint64_t>(camera.is_primary));
    }
    const auto renderer_value = object.mesh_renderer;
    if (renderer_value.has_value()) {
      const auto& renderer = *renderer_value;
      number(1U);
      text(renderer.mesh_uri);
      text(renderer.material_uri);
    } else {
      number(0U);
    }
  }
  std::string version = "prefab-description-v1:";
  constexpr std::string_view digits = "0123456789abcdef";
  for (const auto byte : hash.digest()) {
    const auto value = std::to_integer<unsigned>(byte);
    version.push_back(digits[value >> 4U]);
    version.push_back(digits[value & 15U]);
  }
  return version;
}

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

gneiss_result prefab_asset_loader::install_prepared(std::string_view uri,
                                                    prefab_description description,
                                                    prefab_asset_lease& out_lease) noexcept {
  out_lease.entry_.reset();
  if (engine::resource::validate_uri(uri) != GNEISS_SUCCESS || description.objects.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    engine::resource::resource_cache::version_key key;
    auto result = file_system_.identify(uri, prefab_resource_type, key.identity);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    // 描述只保存依赖 URI；渲染资产版本由实例独立持有的租约固定。
    key.version = description_version(description);
    std::shared_ptr<const engine::resource::resource_cache::entry> entry;
    result = cache_.acquire(
        key,
        [&description](std::shared_ptr<void>& resource) {
          resource = std::make_shared<prefab_description>(std::move(description));
          return GNEISS_SUCCESS;
        },
        entry);
    if (result == GNEISS_SUCCESS) {
      out_lease.entry_ = std::move(entry);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result prefab_asset_loader::acquire(std::string_view uri, prefab_asset_lease& out_lease,
                                           scene_diagnostic& out_diagnostic) noexcept {
  out_lease.entry_.reset();
  out_diagnostic = {};
  if (engine::resource::validate_uri(uri) != GNEISS_SUCCESS) {
    fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "Prefab URI 无效");
    return out_diagnostic.result;
  }

  std::shared_ptr<const engine::resource::resource_cache::entry> entry;
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

gneiss_result prefab_asset_loader::reload(std::string_view uri, prefab_asset_lease& out_lease,
                                          scene_diagnostic& out_diagnostic) noexcept {
  out_lease.entry_.reset();
  out_diagnostic = {};
  if (engine::resource::validate_uri(uri) != GNEISS_SUCCESS) {
    fail(out_diagnostic, GNEISS_ERROR_INVALID_ARGUMENT, "", "Prefab URI 无效");
    return out_diagnostic.result;
  }
  std::shared_ptr<const engine::resource::resource_cache::entry> entry;
  const auto result = cache_.reload(
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
      fail(out_diagnostic, result, "", "无法重新加载 Prefab 资产");
    }
    return result;
  }
  out_lease.entry_ = std::move(entry);
  return GNEISS_SUCCESS;
}

} // namespace gneiss::scene_internal
