// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/function/render/backend/granit/backend.hpp"
#include "engine/function/render/detail/executor.hpp"
#include "engine/function/render/detail/resource_storage.hpp"
#include "engine/function/render/detail/scene.hpp"
#include "engine/function/render/service.hpp"
#include <deque>
#include <set>
namespace gneiss::render {
struct service::implementation final {
public:
  explicit implementation(std::uint64_t memory_limit)
      : resources(memory_limit), targets(resources.domain_), scenes(resources.domain_) {}
  ~implementation() { executor_.stop(); }
  // 接受侧资源与场景平级，由服务唯一实现拥有。
  render::detail::resource_storage resources;
  rid_table<target_description> targets;
  result<void> retire_target(rid id) {
    return dispatch([this, id] { backend_.destroy_target(id); });
  }
  struct scene_slot {
    std::set<std::uint64_t> accepted;
    std::uint64_t last_key{};
    std::shared_ptr<render::scene> content = std::make_shared<render::scene>();
  };
  rid_table<scene_slot> scenes;
  bool scenes_closed{};
  result<rid> create_scene();
  result<void> submit_scene(scene_commands&&);
  result<void> destroy_scene(rid);
  using capture_operation = std::function<result<detail::frame_packet>()>;
  result<capture_operation> prepare_capture(const frame_request&, std::vector<target_description>);
  result<detail::scene_resource_pin> pin(const mesh_instance_description&);
  [[nodiscard]] gneiss::error initialize(const native_window_info& window,
                                         std::span<const std::byte> environment_asset = {},
                                         float environment_intensity = 1.0F,
                                         float environment_rotation_radians = 0.0F,
                                         queue_options queues = {});
  error request_environment(std::shared_ptr<const std::vector<std::byte>>, float, float);
  [[nodiscard]] gneiss::error shutdown(device_stats& stats);
  [[nodiscard]] gneiss::error finish_frames();
  void set_completion_wakeup(std::shared_ptr<wakeup_signal> notification) noexcept {
    executor_.set_completion_wakeup(std::move(notification));
  }
  [[nodiscard]] result<bool> can_prepare_frame();
  [[nodiscard]] result<std::uint64_t> submit(render::detail::render_frame_prepare prepare,
                                             frame_policy policy = frame_policy::replaceable);
  result<void> dispatch(std::function<void()> operation, std::size_t bytes = 0,
                        std::weak_ptr<const void> payload = {}) {
    if (!executor_.is_running()) {
      operation();
      return {};
    }
    auto accepted = executor_.submit_command(
        [operation = std::move(operation)](const render::detail::render_command_reporter&) {
          operation();
          return error::success;
        },
        render::detail::render_command_policy::once, bytes, std::move(payload));
    return accepted ? result<void>{} : result<void>{accepted.error()};
  }
  [[nodiscard]] result<std::optional<frame_completion>> try_take_required_completion();
  [[nodiscard]] queue_stats query_performance_stats() const noexcept;
  /** 仅在 initialize 完成后由宿主读取；设备重建必须重新发布。 */
  [[nodiscard]] texture_prepare_profile texture_profile() const noexcept {
    return backend_.texture_profile();
  }

private:
  error collect_completions();
  render::backend::granit::backend backend_;
  // 执行器先停止，后端及其资源才能析构。
  render::detail::threaded_render_executor executor_;
  std::deque<frame_completion> required_frame_completions_;
  bool pending_recreate_{};
};
} // namespace gneiss::render
