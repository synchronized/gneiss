// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_RESOURCE_CACHE_HPP_
#define GNEISS_ASSET_RESOURCE_CACHE_HPP_

#include "engine/resource/types/asset_identity.hpp"
#include <gneiss/engine/core/result.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gneiss::engine::resource {

enum class resource_state : std::uint8_t { unloaded, loading, ready, failed };

class resource_cache final {
public:
  resource_cache() = default;
  resource_cache(const resource_cache&) = delete;
  resource_cache& operator=(const resource_cache&) = delete;
  struct entry final {
    std::string uri;
    std::shared_ptr<const asset_source_domain> source;
    std::uint32_t type = 0;
    resource_state state = resource_state::unloaded;
    gneiss_result failure = GNEISS_SUCCESS;
    std::shared_ptr<void> resource;
  };

  using loader = std::function<gneiss_result(std::shared_ptr<void>&)>;

  [[nodiscard]] gneiss_result
  acquire(std::string_view uri, std::uint32_t type, const loader& load,
          std::shared_ptr<const entry>& out_entry,
          std::shared_ptr<const asset_source_domain> source = {}) noexcept;
  /** 强制重新加载并在成功后替换缓存项；失败时保留原缓存。 */
  [[nodiscard]] gneiss_result
  reload(std::string_view uri, std::uint32_t type, const loader& load,
         std::shared_ptr<const entry>& out_entry,
         std::shared_ptr<const asset_source_domain> source = {}) noexcept;
  void release_unused() noexcept;
  /** 只观察当前身份，不触发加载；返回的弱引用不会阻止卸载。 */
  [[nodiscard]] std::weak_ptr<const entry> observe(std::string_view uri) const;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
  std::unordered_map<std::string, std::shared_ptr<const entry>> entries_;
};

} // namespace gneiss::engine::resource

#endif
