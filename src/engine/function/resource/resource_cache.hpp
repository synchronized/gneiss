// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_RESOURCE_CACHE_HPP_
#define GNEISS_ASSET_RESOURCE_CACHE_HPP_

#include "engine/core/result.hpp"
#include "engine/resource/asset_identity.hpp"
#include "engine/resource/asset_ref.hpp"

#include "engine/core/type_id.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace gneiss::engine {

class resource_cache final {
public:
  using key = std::tuple<std::shared_ptr<const files::source_domain>, std::string, type_id>;
  resource_cache() = default;
  resource_cache(const resource_cache&) = delete;
  resource_cache& operator=(const resource_cache&) = delete;
  struct entry final {
    std::string uri;
    std::shared_ptr<const files::source_domain> source;
    type_id type{engine::runtime_type_of<void>()};
    bool is_loading{true}; // 仅在同步创建回调期间阻止同键递归。
    std::shared_ptr<const void> resource;
    /** 验证类型后共享运行对象的原控制块，不把缓存条目暴露给类型化使用者。 */
    template <typename T> [[nodiscard]] asset_ref<const T> as() const noexcept {
      return type == runtime_type_of<T>()
                 ? asset_ref<const T>{std::static_pointer_cast<const T>(resource)}
                 : asset_ref<const T>{};
    }
  };

  using loader = std::function<result<std::shared_ptr<const void>>()>;

  /** 所属线程统一加载入口；默认复用，reload 成功后替换，失败保留旧条目。
   * 回调仅在需要加载时执行；同键递归加载返回 invalid_state。 */
  [[nodiscard]] result<asset_ref<const entry>>
  load(std::string_view uri, type_id type, const loader& decode,
       std::shared_ptr<const files::source_domain> source = {}, bool reload = false) noexcept;
  void release_unused() noexcept;
  /** 只观察当前身份，不触发加载；返回的弱引用不会阻止卸载。 */
  [[nodiscard]] std::weak_ptr<const entry>
  observe(std::string_view uri, type_id type,
          std::shared_ptr<const files::source_domain> source = {}) const;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
  std::map<key, asset_ref<const entry>> entries_;
};

} // namespace gneiss::engine

#endif
