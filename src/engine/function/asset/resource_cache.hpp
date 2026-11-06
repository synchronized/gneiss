// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_RESOURCE_CACHE_HPP_
#define GNEISS_ASSET_RESOURCE_CACHE_HPP_

#include "detail/asset_entry.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_ref.hpp"

#include "engine/core/type_id.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace gneiss::engine {

class resource_cache final {
public:
  resource_cache() = default;
  resource_cache(const resource_cache&) = delete;
  resource_cache& operator=(const resource_cache&) = delete;
  [[nodiscard]] std::shared_ptr<detail::asset_entry> acquire(const detail::asset_key& identity);
  void clear_loading() noexcept;

  using loader = std::function<result<std::shared_ptr<const void>>()>;

  /** 所属线程统一加载入口；默认复用，reload 成功后替换，失败保留旧条目。
   * 回调仅在需要加载时执行；同键递归加载返回 invalid_state。 */
  [[nodiscard]] result<asset_ref<const detail::asset_cache_value>>
  load(std::string_view uri, type_id type, const loader& decode,
       std::shared_ptr<const files::source_domain> source = {}, bool reload = false,
       std::size_t source_bytes = 0, std::string parameters = {},
       asset_load_metrics metrics = {}) noexcept;
  void release_unused() noexcept;
  /** 只观察当前身份，不触发加载；返回的弱引用不会阻止卸载。 */
  [[nodiscard]] std::weak_ptr<const detail::asset_cache_value>
  observe(std::string_view uri, type_id type,
          std::shared_ptr<const files::source_domain> source = {},
          std::string parameters = {}) const;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::map<detail::asset_key, std::shared_ptr<detail::asset_entry>> entries_;
};

} // namespace gneiss::engine

#endif
