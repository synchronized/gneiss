// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RUNTIME_SCENE_SERVICE_HPP_
#define GNEISS_RUNTIME_SCENE_SERVICE_HPP_

#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_batch_types.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/scene.hpp"

#include "engine/function/asset/asset_batch_request.hpp"
#include "runtime/loading/scene_load_types.hpp"
#include <optional>
#include <thread>
#include <variant>

namespace gneiss::engine {}

namespace gneiss::engine {
class resource_load_group;
}

namespace gneiss::runtime {

class scene_service;

/** 场景打开请求；仅所属线程使用，移动转移观察权，不保活宿主。
 * 丢弃未激活请求会释放候选。关闭后请求失效；查询不推进加载。 */
class scene_open_request final {
public:
  scene_open_request() = default;
  GNEISS_API ~scene_open_request();
  GNEISS_API scene_open_request(scene_open_request&&) noexcept;
  GNEISS_API scene_open_request& operator=(scene_open_request&&) noexcept;
  scene_open_request(const scene_open_request&) = delete;
  scene_open_request& operator=(const scene_open_request&) = delete;
  [[nodiscard]] GNEISS_API explicit operator bool() const noexcept;
  [[nodiscard]] GNEISS_API bool ready() const;
  [[nodiscard]] GNEISS_API engine::error status() const;
  [[nodiscard]] GNEISS_API std::optional<scene_load_progress> progress() const;
  /** 消费失败或取消回执；成功候选须通过 scene_service::activate 消费。 */
  [[nodiscard]] GNEISS_API engine::result<scene_load_completion> take();
  [[nodiscard]] GNEISS_API engine::error cancel();
  GNEISS_API void reset() noexcept;

private:
  friend class scene_service;
  scene_open_request(std::weak_ptr<scene_service*> owner, std::uint64_t id)
      : owner_(std::move(owner)), id_(id) {}
  scene_service* owner() const noexcept;
  std::weak_ptr<scene_service*> owner_;
  std::uint64_t id_{};
};

/** 原位场景重载请求；查询与领取不推进工作，放弃即取消未应用的重载。 */
class scene_reload_request final {
public:
  scene_reload_request() = default;
  GNEISS_API ~scene_reload_request();
  GNEISS_API scene_reload_request(scene_reload_request&&) noexcept;
  GNEISS_API scene_reload_request& operator=(scene_reload_request&&) noexcept;
  scene_reload_request(const scene_reload_request&) = delete;
  scene_reload_request& operator=(const scene_reload_request&) = delete;
  [[nodiscard]] GNEISS_API explicit operator bool() const noexcept;
  [[nodiscard]] GNEISS_API bool ready() const;
  [[nodiscard]] GNEISS_API engine::result<scene_reload_completion> take();
  GNEISS_API void reset() noexcept;

private:
  friend class scene_service;
  scene_reload_request(std::weak_ptr<scene_service*> owner, std::uint64_t id)
      : owner_(std::move(owner)), id_(id) {}
  scene_service* owner() const noexcept;
  std::weak_ptr<scene_service*> owner_;
  std::uint64_t id_{};
};

/** 场景生命周期的唯一拥有者；全部调用限宿主线程。
 * 借用的资产管理器与渲染服务必须存活至 shutdown 返回。
 * 场景关闭放弃自身观察者，不停止资产管理器的共享工作。
 */
class scene_service final {
public:
  scene_service();
  ~scene_service() noexcept;
  [[nodiscard]] GNEISS_API engine::result<scene_open_request> open(std::string_view uri);
  [[nodiscard]] GNEISS_API engine::result<scene_load_completion>
  activate(scene_open_request&& request);
  scene_service(const scene_service&) = delete;
  scene_service& operator=(const scene_service&) = delete;

  [[nodiscard]] gneiss::engine::error initialize(gneiss::engine::asset_manager& resources);
  [[nodiscard]] gneiss::engine::error configure(gneiss::engine::render_service& renderer);
  /** 激活后的视觉资源独立完成，不影响结构激活结果；所属线程消费诊断。 */
  [[nodiscard]] bool take_visual_completion(gneiss::engine::asset_load_completion& output);
  [[nodiscard]] bool visual_loading() const noexcept { return visuals_.loading(); }
  /** 请求停止加载，不等待 CPU/I/O；继续 advance 至 stopped，再释放依赖。 */
  void request_stop();
  [[nodiscard]] bool stopped() const noexcept;
  void shutdown();

  [[nodiscard]] bool can_start_load() const noexcept;
  [[nodiscard]] bool can_activate() const noexcept;
  [[nodiscard]] bool loading() const noexcept;
  [[nodiscard]] scene_retirement_statistics retirement() const noexcept;
  [[nodiscard]] gneiss::engine::world* native_world() const noexcept;
  /** 异步重载已激活场景中的来源结构；不切换 World。 */
  [[nodiscard]] GNEISS_API engine::result<scene_reload_request>
  reload(scene_instance_id instance, std::string_view uri, bool prefab = false);

private:
  friend class scene_open_request;
  friend class scene_reload_request;
  engine::error request_reload(scene_instance_id, std::string_view, bool);
  friend class application;
  void advance();
  engine::result<scene_load_completion> activate(std::uint64_t request);
  engine::result<std::uint64_t> request_scene(std::string_view uri, std::uint64_t session,
                                              std::uint64_t revision);
  bool scene_progress(scene_load_progress&) const;
  engine::error cancel_scene(std::uint64_t request);
  std::shared_ptr<scene_service*> lifetime_;
  bool owns(std::uint64_t id) const noexcept;
  void abandon(std::uint64_t id) noexcept;
  // 仅为已激活场景的来源热更新保留输入；World 不持有作者内容或覆盖。
  struct source_state {
    scene_instance_id instance;
    engine::ref<const engine::scene_content> content;
    std::map<engine::object_address, engine::scene_object_association> objects;
    bool synchronized = true;
  };
  std::unique_ptr<source_state> source_;
  engine::result<std::vector<engine::render_resource_source>>
  apply_source(engine::ref<const engine::scene_content> content);
  struct reload_task {
    std::uint64_t id{};
    scene_instance_id instance;
    std::variant<engine::asset_request<engine::scene>, engine::asset_request<engine::prefab>>
        descriptions;
    engine::error result{engine::error::not_ready};
    std::vector<engine::scene_object_binding> objects;
  };
  void advance_reload();
  std::unique_ptr<reload_task> reload_;
  void advance_visuals();
  void advance_stop();
  bool stopping_{};
  /** 场景视觉加载的私有状态；只借用请求组，不拥有资产或服务。 */
  struct visual_load_state {
    std::vector<engine::render_resource_source> queue;
    std::size_t cursor{};
    engine::asset_batch_request request;
    bool pending{};
    std::optional<engine::asset_load_completion> completion;
    bool loading() const noexcept { return pending || !queue.empty(); }
    void begin(std::vector<engine::render_resource_source> sources);
    void advance(engine::resource_load_group& group);
    bool take(engine::asset_load_completion& output);
    void cancel(engine::resource_load_group& group);
    void stop(engine::resource_load_group* group);
    void reset() noexcept;
  } visuals_;
  gneiss::engine::asset_manager* resources_{};
  engine::world_owner active_;
  engine::world_owner retired_;
  scene_retirement_statistics retirement_;
  // 只在加载期间持有；完成回执消费前仍占用请求槽位。
  struct load_task;
  void advance_load();
  void advance_load_impl();
  void stop_load() noexcept;
  void finish(gneiss::engine::error result, scene_load_phase phase, std::string message = {});
  void sample_budget(scene_load_progress& value) const;
  void check_owner() const;
  gneiss::engine::render_service* renderer_{};
  std::unique_ptr<load_task> pending_;
  std::optional<scene_load_completion> completed_;
  std::uint64_t sequence_{};
  const std::thread::id owner_{std::this_thread::get_id()};
  std::unique_ptr<engine::resource_load_group> loading_;
};

} // namespace gneiss::runtime

#endif
