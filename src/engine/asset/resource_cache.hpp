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
  resource_cache() = default;
  resource_cache(const resource_cache&) = delete;
  resource_cache& operator=(const resource_cache&) = delete;
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
  struct version_publication {
    version_key key;
    std::shared_ptr<const entry> value;
  };
  /** 原子发布已确认的固定版本候选；同键仅接受同一条目，冲突不覆盖。
   * 失败不改变缓存；调用方继续持有候选，负责上传撤回及资源释放。 */
  [[nodiscard]] gneiss_result
  publish_versions(std::span<const version_publication> values) noexcept;

  /** 普通获取的选择域；configuration 由调用方完整编码兼容准备配置，空串表示默认域。
   * 不包含内容版本，不允许跨来源或不兼容配置复用选择。 */
  struct selection_key {
    asset_identity identity;
    std::string configuration;
    bool operator==(const selection_key&) const = default;
  };
  struct version_selection {
    selection_key selection;
    version_key version;
  };
  /** 跨推进步骤保留的选择事务。仅所属线程使用，必须早于缓存销毁。
   * 准备后其他采用返回 NOT_READY；查询与版本发布仍可继续。销毁等于取消。
   * commit 只交换已准备映射，不分配；重复提交无效果。 */
  class prepared_adoption final {
  public:
    ~prepared_adoption();
    prepared_adoption(const prepared_adoption&) = delete;
    prepared_adoption& operator=(const prepared_adoption&) = delete;
    void commit() noexcept;

  private:
    friend class resource_cache;
    struct state;
    explicit prepared_adoption(std::unique_ptr<state> value) noexcept;
    std::unique_ptr<state> state_;
  };
  /** 输出必须为空。成功后保留候选租约与采用权，不改变默认选择。 */
  [[nodiscard]] gneiss_result prepare_adoption(std::span<const version_selection> values,
                                               std::unique_ptr<prepared_adoption>& output) noexcept;
  /** 所属线程显式采用已发布版本；整批校验成功才改变选择，重复选择域拒绝。
   * 发布版本本身不会采用；调用方负责只在加载事务接受后调用。 */
  [[nodiscard]] gneiss_result adopt_versions(std::span<const version_selection> values) noexcept;
  /** 先准备选择映射，再调用 accept 提交使用者，成功后无分配交换映射。
   * accept 失败或抛出异常必须保持使用者原状态；回调不得修改本缓存或递归采用。
   * 仅所属线程调用，回调期间候选资源由缓存持有；不允许异步保留回调。 */
  [[nodiscard]] gneiss_result adopt_versions(std::span<const version_selection> values,
                                             const std::function<gneiss_result()>& accept) noexcept;
  /** 不读文件、不按版本排序；未采用或资源已回收时返回空弱引用。 */
  [[nodiscard]] std::weak_ptr<const entry> observe_selected(const selection_key& key) const;

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
  struct selection_hash {
    std::size_t operator()(const selection_key& key) const noexcept;
  };
  // 旧 URI 接口使用空来源、零类型与空版本的保留键，保留原来的类型冲突和热重载语义。
  [[nodiscard]] static version_key legacy_key(std::string_view uri);
  bool adoption_reserved_{};
  std::uint64_t revision_{};
  std::unordered_map<version_key, std::shared_ptr<const entry>, key_hash> entries_;
  // 仅保存选择元数据，资源仍由 entries_ 和租约拥有；不阻止无使用者回收。
  std::unordered_map<selection_key, version_key, selection_hash> selections_;
};

} // namespace gneiss::asset_internal

#endif
