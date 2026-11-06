// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/asset/resource_cache.hpp"
#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_ref.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <new>
#include <unordered_set>

namespace gneiss::engine {

result<asset_ref<const detail::asset_cache_value>>
resource_cache::load(std::string_view uri, type_id type, const loader& decode,
                     std::shared_ptr<const files::source_domain> source, bool reload,
                     std::size_t source_bytes, std::string parameters,
                     asset_load_metrics metrics) noexcept {
  using engine::error;
  if (type == engine::runtime_type_of<void>() || !decode || validate_uri(uri) != error::success)
    return error::invalid_argument;
  try {
    const detail::asset_key identity{source, std::string(uri), type, std::move(parameters)};
    auto record = acquire(identity);
    auto existing = record->cached ? record->cached->lock() : std::shared_ptr<const detail::asset_cache_value>{};
    if (existing) {
      if (existing->is_loading)
        return error::invalid_state;
      if (!reload) {
        return asset_ref<const detail::asset_cache_value>{existing};
      }
    }
    auto value = std::make_shared<detail::asset_cache_value>();
    value->uri = uri;
    value->source = std::move(source);
    value->type = type;
    value->source_bytes = source_bytes;
    value->metrics = std::move(metrics);
    // 加载期间保活旧值并阻止同键递归；失败（包括异常）恢复原条目。
    const auto previous = record->cached;
    record->cached = value;
    error status;
    try {
      auto decoded = decode();
      status = decoded ? error::success : decoded.error();
      if (decoded)
        value->resource = std::move(*decoded);
      if (status == error::success && !value->resource)
        status = error::internal;
    } catch (const std::bad_alloc&) {
      status = error::out_of_memory;
    } catch (...) {
      status = error::internal;
    }
    if (status != error::success) {
      record->cached = previous && !previous->expired() ? previous : std::nullopt;
      return status;
    }
    value->is_loading = false;
    return asset_ref<const detail::asset_cache_value>{std::move(value)};
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}

std::shared_ptr<detail::asset_entry> resource_cache::acquire(const detail::asset_key& identity) {
  auto found = entries_.find(identity);
  if (found != entries_.end())
    return found->second;
  auto record = std::make_shared<detail::asset_entry>();
  record->identity = identity;
  entries_.emplace(identity, record);
  return record;
}
void resource_cache::clear_loading() noexcept {
  for (auto& [key, record] : entries_)
    record->loading.reset();
}
std::size_t resource_cache::size() const noexcept {
  return std::ranges::count_if(entries_,
                               [](const auto& item) { return item.second->cached.has_value(); });
}

void resource_cache::release_unused() noexcept {
  // 弱索引不拥有资源，销毁由最后一个使用者触发；这里只清理过期键。
  std::erase_if(entries_, [](const auto& item) {
    const auto& record = *item.second;
    return !record.loading && (!record.cached || record.cached->expired());
  });
}

std::weak_ptr<const detail::asset_cache_value>
resource_cache::observe(std::string_view uri, type_id type,
                        std::shared_ptr<const files::source_domain> source,
                        std::string parameters) const {
  const auto found =
      entries_.find(detail::asset_key{std::move(source), std::string(uri), type, std::move(parameters)});
  return found == entries_.end() || !found->second->cached
             ? std::weak_ptr<const detail::asset_cache_value>{}
             : *found->second->cached;
}

} // namespace gneiss::engine
