// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/resource/resource_cache.hpp"
#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_ref.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <new>
#include <unordered_set>

namespace gneiss::engine {

result<asset_ref<const resource_cache::entry>>
resource_cache::load(std::string_view uri, type_id type, const loader& decode,
                     std::shared_ptr<const files::source_domain> source, bool reload,
                     std::size_t source_bytes, std::string parameters,
                     asset_data_metrics metrics) noexcept {
  using engine::error;
  if (type == engine::runtime_type_of<void>() || !decode || validate_uri(uri) != error::success)
    return error::invalid_argument;
  try {
    const key identity{source, std::string(uri), type, std::move(parameters)};
    auto found = entries_.find(identity);
    auto existing = found == entries_.end() ? std::shared_ptr<const entry>{} : found->second.lock();
    if (existing) {
      if (existing->is_loading)
        return error::invalid_state;
      if (!reload) {
        return asset_ref<const entry>{existing};
      }
    }
    auto value = std::make_shared<entry>();
    value->uri = uri;
    value->source = std::move(source);
    value->type = type;
    value->source_bytes = source_bytes;
    value->metrics = std::move(metrics);
    // 加载期间保活旧值并阻止同键递归；失败（包括异常）恢复原条目。
    const auto previous = found == entries_.end() ? std::weak_ptr<const entry>{} : found->second;
    entries_.insert_or_assign(identity, value);
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
    found = entries_.find(identity);
    if (status != error::success) {
      if (previous.lock())
        found->second = previous;
      else
        entries_.erase(found);
      return status;
    }
    value->is_loading = false;
    return asset_ref<const entry>{std::move(value)};
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}

void resource_cache::release_unused() noexcept {
  // 弱索引不拥有资源，销毁由最后一个使用者触发；这里只清理过期键。
  std::erase_if(entries_, [](const auto& item) { return item.second.expired(); });
}

std::weak_ptr<const resource_cache::entry>
resource_cache::observe(std::string_view uri, type_id type,
                        std::shared_ptr<const files::source_domain> source,
                        std::string parameters) const {
  const auto found =
      entries_.find(key{std::move(source), std::string(uri), type, std::move(parameters)});
  return found == entries_.end() ? std::weak_ptr<const entry>{} : found->second;
}

} // namespace gneiss::engine
