// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset/resource_cache.h"

#include "asset/asset_uri.h"

#include <new>
#include <set>

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

gneiss_result resource_cache::reload(std::string_view uri, std::uint32_t type, const loader& load,
                                     std::shared_ptr<const entry>& out_entry) noexcept {
  out_entry.reset();
  if (type == 0U || !load || validate_uri(uri) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    const auto found = entries_.find(std::string(uri));
    if (found != entries_.end() && found->second->type != type) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    auto value = std::make_shared<entry>();
    value->uri = uri;
    value->type = type;
    value->state = resource_state::loading;
    std::shared_ptr<void> resource;
    const auto result = load(resource);
    if (result != GNEISS_SUCCESS || resource == nullptr) {
      return result == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : result;
    }
    value->resource = std::move(resource);
    value->state = resource_state::ready;
    entries_.insert_or_assign(value->uri, value);
    out_entry = std::move(value);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result resource_cache::reload_transaction(
    std::span<const reload_request> requests,
    std::vector<std::shared_ptr<const entry>>& out_entries) noexcept {
  out_entries.clear();
  if (requests.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::set<std::string> uris;
    resource_cache staging;
    staging.entries_ = entries_;
    std::vector<std::shared_ptr<const entry>> committed;
    committed.reserve(requests.size());
    for (const auto& request : requests) {
      if (request.type == 0U || !request.load || validate_uri(request.uri) != GNEISS_SUCCESS ||
          !uris.insert(request.uri).second) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto found = staging.entries_.find(request.uri);
      if (found != staging.entries_.end() && found->second->type != request.type) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      staging.entries_.erase(request.uri);
      auto candidate = std::make_shared<entry>();
      candidate->uri = request.uri;
      candidate->type = request.type;
      candidate->state = resource_state::loading;
      std::shared_ptr<void> resource;
      const auto loaded = request.load(staging, resource);
      if (loaded != GNEISS_SUCCESS || resource == nullptr) {
        return loaded == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : loaded;
      }
      candidate->resource = std::move(resource);
      candidate->state = resource_state::ready;
      staging.entries_.insert_or_assign(candidate->uri, candidate);
      committed.push_back(candidate);
    }
    entries_.swap(staging.entries_);
    out_entries = std::move(committed);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
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

} // namespace gneiss::asset_internal
