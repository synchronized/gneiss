// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/cache/resource_cache.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_ref.hpp"

#include "engine/resource/asset_uri.hpp"

#include <algorithm>
#include <new>
#include <unordered_set>

namespace gneiss::engine {

gneiss::engine::result resource_cache::load(std::string_view uri, gneiss::engine::type_id type,
                                            const loader& decode, asset_ref<const entry>& out_entry,
                                            std::shared_ptr<const file_source_domain> source,
                                            bool reload) noexcept {
  using gneiss::engine::result;
  out_entry.reset();
  if (type == gneiss::engine::runtime_type_of<void>() || !decode ||
      validate_uri(uri) != result::success)
    return result::invalid_argument;
  try {
    const key identity{source, std::string(uri), type};
    auto found = entries_.find(identity);
    if (found != entries_.end()) {
      if (found->second->state == resource_state::loading)
        return result::invalid_state;
      if (!reload && found->second->state == resource_state::ready) {
        out_entry = found->second;
        return result::success;
      }
    }
    auto value = std::make_shared<entry>();
    value->uri = uri;
    value->source = std::move(source);
    value->type = type;
    value->state = resource_state::loading;
    // 加载期间保活旧值并阻止同键递归；失败（包括异常）恢复原条目。
    const auto previous = found == entries_.end() ? asset_ref<const entry>{} : found->second;
    entries_.insert_or_assign(identity, asset_ref<const entry>{value});
    result status;
    try {
      status = decode(value->resource);
      if (status == result::success && !value->resource)
        status = result::internal;
    } catch (const std::bad_alloc&) {
      status = result::out_of_memory;
    } catch (...) {
      status = result::internal;
    }
    found = entries_.find(identity);
    if (status != result::success) {
      if (previous)
        found->second = previous;
      else
        entries_.erase(found);
      return status;
    }
    value->state = resource_state::ready;
    out_entry = asset_ref<const entry>{std::move(value)};
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

void resource_cache::release_unused() noexcept {
  // 重复扫描以清理依赖链：释放 Material 后，其 Texture 可能在下一轮才变为未使用。
  bool removed = false;
  do {
    removed = false;
    for (auto iterator = entries_.begin(); iterator != entries_.end();) {
      // 同时考虑条目租约和直接类型化引用，避免对象仍在使用时丢失 URI 身份。
      if (iterator->second.use_count() == 1 && iterator->second->resource.use_count() <= 1) {
        iterator = entries_.erase(iterator);
        removed = true;
      } else {
        ++iterator;
      }
    }
  } while (removed);
}

std::weak_ptr<const resource_cache::entry>
resource_cache::observe(std::string_view uri, gneiss::engine::type_id type,
                        std::shared_ptr<const file_source_domain> source) const {
  const auto found = entries_.find(key{std::move(source), std::string(uri), type});
  return found == entries_.end() ? std::weak_ptr<const entry>{}
                                 : std::weak_ptr<const entry>{found->second.shared()};
}

} // namespace gneiss::engine
