// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/cache/resource_cache.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/asset_ref.hpp"

#include "engine/resource/vfs/asset_uri.hpp"

#include <algorithm>
#include <new>
#include <unordered_set>

namespace gneiss::engine::resource {

gneiss::engine::core::result
resource_cache::acquire(std::string_view uri, std::type_index type, const loader& load,
                        asset_ref<const entry>& out_entry,
                        std::shared_ptr<const asset_source_domain> source) noexcept {
  out_entry.reset();
  if (type == typeid(void) || !load || validate_uri(uri) != gneiss::engine::core::result::success) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    const key identity{source, std::string(uri), type};
    const auto found = entries_.find(identity);
    if (found != entries_.end()) {
      if ((found->second->type != type || found->second->source != source)) {
        return gneiss::engine::core::result::invalid_argument;
      }
      if (found->second->state == resource_state::ready) {
        out_entry = found->second;
        return gneiss::engine::core::result::success;
      }
      if (found->second->state == resource_state::loading) {
        return gneiss::engine::core::result::invalid_state;
      }
      entries_.erase(found); // 失败不永久缓存；下一次获取重新尝试。
    }

    auto value = std::make_shared<entry>();
    value->uri = uri;
    value->source = std::move(source);
    value->type = type;
    value->state = resource_state::loading;
    entries_.emplace(identity, asset_ref<const entry>{value});

    std::shared_ptr<const void> resource;
    const auto result = load(resource);
    if (result != gneiss::engine::core::result::success || resource == nullptr) {
      value->state = resource_state::failed;
      value->failure = result == gneiss::engine::core::result::success
                           ? gneiss::engine::core::result::internal
                           : result;
      entries_.erase(identity);
      return value->failure;
    }
    value->resource = std::move(resource);
    value->state = resource_state::ready;
    out_entry = asset_ref<const entry>{std::move(value)};
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
resource_cache::reload(std::string_view uri, std::type_index type, const loader& load,
                       asset_ref<const entry>& out_entry,
                       std::shared_ptr<const asset_source_domain> source) noexcept {
  out_entry.reset();
  if (type == typeid(void) || !load || validate_uri(uri) != gneiss::engine::core::result::success) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    const key identity{source, std::string(uri), type};
    const auto found = entries_.find(identity);
    if (found != entries_.end() &&
        (found->second->type != type || found->second->source != source)) {
      return gneiss::engine::core::result::invalid_argument;
    }
    auto value = std::make_shared<entry>();
    value->uri = uri;
    value->source = std::move(source);
    value->type = type;
    value->state = resource_state::loading;
    std::shared_ptr<const void> resource;
    const auto result = load(resource);
    if (result != gneiss::engine::core::result::success || resource == nullptr) {
      return result == gneiss::engine::core::result::success
                 ? gneiss::engine::core::result::internal
                 : result;
    }
    value->resource = std::move(resource);
    value->state = resource_state::ready;
    entries_.insert_or_assign(identity, asset_ref<const entry>{value});
    out_entry = asset_ref<const entry>{std::move(value)};
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

void resource_cache::release_unused() noexcept {
  // 重复扫描以清理依赖链：释放 Material 后，其 Texture 可能在下一轮才变为未使用。
  bool removed = false;
  do {
    removed = false;
    for (auto iterator = entries_.begin(); iterator != entries_.end();) {
      // map 与调用方或依赖租约之外没有持有者时即可释放。
      if (iterator->second.use_count() == 1) {
        iterator = entries_.erase(iterator);
        removed = true;
      } else {
        ++iterator;
      }
    }
  } while (removed);
}

std::weak_ptr<const resource_cache::entry>
resource_cache::observe(std::string_view uri, std::type_index type,
                        std::shared_ptr<const asset_source_domain> source) const {
  const auto found = entries_.find(key{std::move(source), std::string(uri), type});
  return found == entries_.end() ? std::weak_ptr<const entry>{}
                                 : std::weak_ptr<const entry>{found->second.shared()};
}

} // namespace gneiss::engine::resource
