// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/resource_cache.h"

#include "asset/asset_uri.h"

#include <new>

namespace gneiss::asset_internal {

gneiss_result resource_cache::acquire(std::string_view uri, std::uint32_t type, const loader& load,
                                      std::shared_ptr<const entry>& out_entry) noexcept {
  out_entry.reset();
  if (type == 0U || !load || validate_uri(uri) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    const auto found = entries_.find(std::string(uri));
    if (found != entries_.end()) {
      if (found->second->type != type) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      if (found->second->state == resource_state::ready) {
        out_entry = found->second;
        return GNEISS_SUCCESS;
      }
      if (found->second->state == resource_state::loading) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      entries_.erase(found); // 失败不永久缓存；下一次获取重新尝试。
    }

    auto value = std::make_shared<entry>();
    value->uri = uri;
    value->type = type;
    value->state = resource_state::loading;
    entries_.emplace(value->uri, value);

    std::shared_ptr<void> resource;
    const auto result = load(resource);
    if (result != GNEISS_SUCCESS || resource == nullptr) {
      value->state = resource_state::failed;
      value->failure = result == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : result;
      entries_.erase(value->uri);
      return value->failure;
    }
    value->resource = std::move(resource);
    value->state = resource_state::ready;
    out_entry = std::move(value);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

void resource_cache::release_unused() noexcept {
  for (auto iterator = entries_.begin(); iterator != entries_.end();) {
    // map 与调用方租约之外没有持有者时即可释放。
    if (iterator->second.use_count() == 1) {
      iterator = entries_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

} // namespace gneiss::asset_internal
