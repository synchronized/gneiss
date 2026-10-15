// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ASSET_RESOURCE_CACHE_HPP_
#define GNEISS_ASSET_RESOURCE_CACHE_HPP_

#include "engine/asset/asset_identity.hpp"
#include <gneiss/engine/core/result.h>

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

  /** 已验证产物的完整版本标识；由调用方包含来源、依赖及准备配置的变化。
   * 本缓存不解释编码、不读取来源，空版本无效；不能用缓存变更序号代替它。
   */
  using version_key = asset_version_key;
  /** 同键只发布一次；不同版本共存。发布后调用方不得原地修改资源。
   * 仅所属线程调用；递归获取正在加载的同键返回 INVALID_STATE，不是异步任务合并接口。
   */
  [[nodiscard]] gneiss_result acquire(const version_key& key, const loader& load,
                                      std::shared_ptr<const entry>& out_entry) noexcept;
  [[nodiscard]] std::weak_ptr<const entry> observe(const version_key& key) const;

  struct reload_request final {
    std::string uri;
    std::uint32_t type = 0U;
    std::function<gneiss_result(resource_cache&, std::shared_ptr<void>&)> load;
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
  /** 只观察当前身份，不触发加载；返回的弱引用不会阻止卸载。 */
  [[nodiscard]] std::weak_ptr<const entry> observe(std::string_view uri) const;
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
  struct key_hash {
    std::size_t operator()(const version_key& key) const noexcept;
  };
  // 旧 URI 接口使用空来源、零类型与空版本的保留键，保留原来的类型冲突和热重载语义。
  [[nodiscard]] static version_key legacy_key(std::string_view uri);
  std::uint64_t revision_{};
  std::unordered_map<version_key, std::shared_ptr<entry>, key_hash> entries_;
};

} // namespace gneiss::asset_internal

#endif
