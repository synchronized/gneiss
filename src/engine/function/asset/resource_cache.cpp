// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/asset/resource_cache.hpp"
#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <new>
#include <unordered_set>

namespace gneiss::engine {

result<detail::cached_asset>
resource_cache::load(std::string_view uri, type_id type, const loader& decode,
                     std::shared_ptr<const files::source_domain> source, bool reload,
                     std::size_t source_bytes, std::string parameters,
                     asset_load_metrics metrics) noexcept try {
  if (type == runtime_type_of<void>() || !decode || validate_uri(uri).failed())
    return error::invalid_argument;
  auto record = acquire({std::move(source), std::string(uri), type, std::move(parameters)});
  if (record->constructing)
    return error::invalid_state;
  auto existing = record->cached ? record->cached->lock() : std::shared_ptr<const asset>{};
  if (existing && !reload)
    return detail::cached_asset{type, std::move(existing), record->metrics, record->source_bytes};
  struct construction_guard {
    bool& active;
    explicit construction_guard(bool& value) : active(value) { active = true; }
    ~construction_guard() { active = false; }
  } guard(record->constructing);
  auto decoded = decode();
  if (!decoded)
    return decoded.error();
  if (!*decoded)
    return error::internal;
  if ((*decoded)->type() != type)
    return error::invalid_argument;
  // 先完成可能分配内存的统计复制，再替换弱引用；失败保留旧记录。
  detail::cached_asset value{type, std::move(*decoded), metrics, source_bytes};
  record->metrics = std::move(metrics);
  record->source_bytes = source_bytes;
  record->cached = value.resource;
  return value;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
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
    return !record.loading && !record.constructing && (!record.cached || record.cached->expired());
  });
}

std::weak_ptr<const asset>
resource_cache::observe(std::string_view uri, type_id type,
                        std::shared_ptr<const files::source_domain> source,
                        std::string parameters) const {
  const auto found =
      entries_.find(detail::asset_key{std::move(source), std::string(uri), type, std::move(parameters)});
  return found == entries_.end() || !found->second->cached ? std::weak_ptr<const asset>{}
                                                           : *found->second->cached;
}

std::optional<detail::cached_asset>
resource_cache::lookup(std::string_view uri, type_id type,
                       std::shared_ptr<const files::source_domain> source,
                       std::string parameters) const {
  const auto found =
      entries_.find({std::move(source), std::string(uri), type, std::move(parameters)});
  if (found == entries_.end() || found->second->constructing || !found->second->cached)
    return std::nullopt;
  const auto& record = *found->second;
  auto value = record.cached->lock();
  if (!value)
    return std::nullopt;
  return detail::cached_asset{type, std::move(value), record.metrics, record.source_bytes};
}
} // namespace gneiss::engine
