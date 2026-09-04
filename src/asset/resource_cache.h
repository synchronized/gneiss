// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_RESOURCE_CACHE_H_
#define GNEISS_ASSET_RESOURCE_CACHE_H_

#include <gneiss/core/result.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gneiss::asset_internal {

enum class resource_state : std::uint8_t { unloaded, loading, ready, failed };

class resource_cache final {
public:
  struct entry final {
    std::string uri;
    std::uint32_t type = 0;
    resource_state state = resource_state::unloaded;
    gneiss_result failure = GNEISS_SUCCESS;
    std::shared_ptr<void> resource;
  };

  using loader = std::function<gneiss_result(std::shared_ptr<void>&)>;

  struct reload_request final {
    std::string uri;
    std::uint32_t type = 0U;
    loader load;
  };

  [[nodiscard]] gneiss_result acquire(std::string_view uri, std::uint32_t type, const loader& load,
                                      std::shared_ptr<const entry>& out_entry) noexcept;
  /** 强制重新加载并在成功后替换缓存项；失败时保留原缓存。 */
  [[nodiscard]] gneiss_result reload(std::string_view uri, std::uint32_t type, const loader& load,
                                     std::shared_ptr<const entry>& out_entry) noexcept;
  /** 先构造全部候选，全部成功后一次提交缓存映射；失败时不改变任何缓存项。 */
  [[nodiscard]] gneiss_result
  reload_transaction(std::span<const reload_request> requests,
                     std::vector<std::shared_ptr<const entry>>& out_entries) noexcept;
  void release_unused() noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
  std::unordered_map<std::string, std::shared_ptr<entry>> entries_;
};

} // namespace gneiss::asset_internal

#endif
