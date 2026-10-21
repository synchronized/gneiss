// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/cache/resource_cache.hpp"

#include "engine/resource/vfs/asset_uri.hpp"

#include <algorithm>
#include <new>
#include <set>
#include <unordered_set>

namespace gneiss::asset_internal {

resource_cache::version_key resource_cache::legacy_key(std::string_view uri) {
  return {.identity = {.source = {}, .uri = std::string(uri), .type = 0U}, .version = {}};
}

std::size_t resource_cache::key_hash::operator()(const version_key& key) const noexcept {
  auto value = std::hash<const asset_source_domain*>{}(key.identity.source.get());
  const auto combine = [&](std::size_t part) { value ^= part + (value << 6U) + (value >> 2U); };
  combine(std::hash<std::string>{}(key.identity.uri));
  combine(std::hash<std::uint32_t>{}(key.identity.type));
  combine(std::hash<std::string>{}(key.version));
  for (const auto& input : key.inputs) {
    combine(std::hash<const asset_source_domain*>{}(input.source.get()));
    combine(std::hash<std::string>{}(input.uri));
    combine(std::hash<std::uint32_t>{}(input.type));
  }
  return value;
}

std::size_t resource_cache::selection_hash::operator()(const selection_key& key) const noexcept {
  auto value = std::hash<const asset_source_domain*>{}(key.identity.source.get());
  const auto combine = [&](std::size_t part) { value ^= part + (value << 6U) + (value >> 2U); };
  combine(std::hash<std::string>{}(key.identity.uri));
  combine(std::hash<std::uint32_t>{}(key.identity.type));
  combine(std::hash<std::string>{}(key.configuration));
  return value;
}

struct resource_cache::prepared_adoption::state {
  resource_cache* owner{};
  std::unordered_map<selection_key, version_key, selection_hash> selections;
  std::vector<std::shared_ptr<const entry>> leases;
  bool changed{};
};
resource_cache::prepared_adoption::prepared_adoption(std::unique_ptr<state> value) noexcept
    : state_(std::move(value)) {}
resource_cache::prepared_adoption::~prepared_adoption() {
  if (state_->owner != nullptr) {
    state_->owner->adoption_reserved_ = false;
  }
}
void resource_cache::prepared_adoption::commit() noexcept {
  if (state_->owner == nullptr) {
    return;
  }
  auto& owner = *state_->owner;
  if (state_->changed) {
    owner.selections_.swap(state_->selections);
    ++owner.revision_;
  }
  owner.adoption_reserved_ = false;
  state_->owner = nullptr;
}
gneiss_result resource_cache::prepare_adoption(std::span<const version_selection> values,
                                               std::unique_ptr<prepared_adoption>& output) noexcept
    try {
  if (values.empty() || output) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (adoption_reserved_) {
    return GNEISS_ERROR_NOT_READY;
  }
  auto staged = std::make_unique<prepared_adoption::state>();
  staged->selections = selections_;
  staged->leases.reserve(values.size());
  std::unordered_set<selection_key, selection_hash> unique;
  for (const auto& value : values) {
    if (!value.selection.identity.source || value.selection.identity.type == 0U ||
        value.selection.identity != value.version.identity || value.version.version.empty() ||
        !unique.insert(value.selection).second) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    const auto published = entries_.find(value.version);
    if (published == entries_.end() || published->second->state != resource_state::ready) {
      return GNEISS_ERROR_NOT_READY;
    }
    staged->leases.push_back(published->second);
    const auto previous = staged->selections.find(value.selection);
    if (previous == staged->selections.end() || previous->second != value.version) {
      staged->selections.insert_or_assign(value.selection, value.version);
      staged->changed = true;
    }
  }
  // 最后一次分配成功后才保留采用权；任何准备失败都不影响其他请求。
  auto candidate = std::unique_ptr<prepared_adoption>(new prepared_adoption(std::move(staged)));
  candidate->state_->owner = this;
  adoption_reserved_ = true;
  output = std::move(candidate);
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

gneiss_result resource_cache::adopt_versions(std::span<const version_selection> values) noexcept {
  return adopt_versions(values, [] { return GNEISS_SUCCESS; });
}
gneiss_result resource_cache::adopt_versions(std::span<const version_selection> values,
                                             const std::function<gneiss_result()>& accept) noexcept
    try {
  if (!accept) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  std::unique_ptr<prepared_adoption> prepared;
  const auto result = prepare_adoption(values, prepared);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  const auto accepted = accept();
  if (accepted == GNEISS_SUCCESS) {
    prepared->commit();
  }
  return accepted;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

std::weak_ptr<const resource_cache::entry>
resource_cache::observe_selected(const selection_key& key) const {
  const auto selected = selections_.find(key);
  return selected == selections_.end() ? std::weak_ptr<const entry>{} : observe(selected->second);
}

gneiss_result resource_cache::acquire(const version_key& key, const loader& load,
                                      std::shared_ptr<const entry>& out_entry) noexcept {
  out_entry.reset();
  if (!key.identity.source || key.identity.type == 0U || key.version.empty() || !load ||
      validate_uri(key.identity.uri) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (const auto& input : key.inputs) {
    if (!input.source || input.type == 0U || validate_uri(input.uri) != GNEISS_SUCCESS) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  try {
    const auto found = entries_.find(key);
    if (found != entries_.end()) {
      if (found->second->state != resource_state::ready) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      out_entry = found->second;
      return GNEISS_SUCCESS;
    }
    auto value = std::make_shared<entry>();
    value->uri = key.identity.uri;
    value->type = key.identity.type;
    value->state = resource_state::loading;
    entries_.emplace(key, value);
    ++revision_;
    // 失败或异常必须撤回占位，允许之后重试；不得覆盖其他版本。
    try {
      std::shared_ptr<void> resource;
      const auto result = load(resource);
      if (result != GNEISS_SUCCESS || !resource) {
        entries_.erase(key);
        return result == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : result;
      }
      value->resource = std::move(resource);
      value->state = resource_state::ready;
      out_entry = std::move(value);
      return GNEISS_SUCCESS;
    } catch (...) {
      entries_.erase(key);
      throw;
    }
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

std::weak_ptr<const resource_cache::entry> resource_cache::observe(const version_key& key) const {
  const auto found = entries_.find(key);
  return found == entries_.end() ? std::weak_ptr<const entry>{} : found->second;
}

gneiss_result
resource_cache::publish_versions(std::span<const version_publication> values) noexcept {
  if (values.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto staged = entries_;
    bool changed = false;
    for (const auto& publication : values) {
      const auto& key = publication.key;
      const auto& value = publication.value;
      if (!key.identity.source || key.identity.type == 0U || key.version.empty() ||
          validate_uri(key.identity.uri) != GNEISS_SUCCESS || !value || !value->resource ||
          value->state != resource_state::ready || value->uri != key.identity.uri ||
          value->type != key.identity.type) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      for (const auto& input : key.inputs) {
        if (!input.source || input.type == 0U || validate_uri(input.uri) != GNEISS_SUCCESS) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
      }
      const auto [found, inserted] = staged.emplace(key, value);
      if (!inserted && found->second != value) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      changed = changed || inserted;
    }
    if (changed) {
      entries_.swap(staged);
      ++revision_;
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result resource_cache::acquire(std::string_view uri, std::uint32_t type, const loader& load,
                                      std::shared_ptr<const entry>& out_entry) noexcept {
  out_entry.reset();
  if (type == 0U || !load || validate_uri(uri) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    const auto found = entries_.find(legacy_key(uri));
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
    entries_.emplace(legacy_key(value->uri), value);
    ++revision_;

    std::shared_ptr<void> resource;
    const auto result = load(resource);
    if (result != GNEISS_SUCCESS || resource == nullptr) {
      value->state = resource_state::failed;
      value->failure = result == GNEISS_SUCCESS ? GNEISS_ERROR_INTERNAL : result;
      entries_.erase(legacy_key(value->uri));
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
    const auto found = entries_.find(legacy_key(uri));
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
    entries_.insert_or_assign(legacy_key(value->uri), value);
    ++revision_;
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
      const auto found = staging.entries_.find(legacy_key(request.uri));
      if (found != staging.entries_.end() && found->second->type != request.type) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      staging.entries_.erase(legacy_key(request.uri));
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
      staging.entries_.insert_or_assign(legacy_key(candidate->uri), candidate);
      committed.push_back(candidate);
    }
    entries_.swap(staging.entries_);
    ++revision_;
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
        ++revision_;
        removed = true;
      } else {
        ++iterator;
      }
    }
  } while (removed);
  std::erase_if(selections_,
                [&](const auto& selected) { return !entries_.contains(selected.second); });
}

std::weak_ptr<const resource_cache::entry> resource_cache::observe(std::string_view uri) const {
  const auto found = entries_.find(legacy_key(uri));
  return found == entries_.end() ? std::weak_ptr<const entry>{} : found->second;
}

} // namespace gneiss::asset_internal
