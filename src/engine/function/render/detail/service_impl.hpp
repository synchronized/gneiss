// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/function/render/backend/granit/backend.hpp"
#include "engine/function/render/detail/executor.hpp"
#include "engine/function/render/detail/render_scene_state.hpp"
#include "engine/function/render/detail/resource_storage.hpp"
#include "engine/function/render/service.hpp"
#include <deque>
namespace gneiss::render {
struct service::implementation final {
public:
  explicit implementation(std::uint64_t memory_limit)
      : resources(memory_limit),
        scenes(
            resources.domain_,
            [this](const mesh_instance_description& description)
                -> result<gneiss::render::detail::scene_resource_pin> {
              const auto* mesh = resources.meshes_.get(description.mesh, resource_type::mesh);
              const auto* material =
                  resources.materials_.get(description.material, resource_type::material);
              if (!mesh || !material)
                return error::invalid_handle;
              return gneiss::render::detail::scene_resource_pin{mesh->identity, material->identity};
            },
            [this](std::function<void()> operation) { return dispatch(std::move(operation)); }) {}
  ~implementation() { executor_.stop(); }
  // 接受侧资源与场景平级，由同一个实现拥有；回调只在实现存活期内使用。
  render::detail::resource_storage resources;
  gneiss::render::detail::render_scene_state scenes;
  [[nodiscard]] gneiss::error initialize(const native_window_info& window,
                                         std::span<const std::byte> environment_asset = {},
                                         float environment_intensity = 1.0F,
                                         float environment_rotation_radians = 0.0F);
  error request_environment(std::shared_ptr<const std::vector<std::byte>>, float, float);
  [[nodiscard]] gneiss::error shutdown(device_stats& stats);
  [[nodiscard]] gneiss::error finish_frames();
  void set_completion_wakeup(std::shared_ptr<wakeup_signal> notification) noexcept {
    executor_.set_completion_wakeup(std::move(notification));
  }
  [[nodiscard]] result<bool> can_prepare_frame();
  [[nodiscard]] result<std::uint64_t> submit(render::detail::render_frame_prepare prepare,
                                             frame_policy policy = frame_policy::replaceable);
  result<void> dispatch(std::function<void()> operation) {
    if (!executor_.is_running()) {
      operation();
      return {};
    }
    auto accepted = executor_.submit_command(
        [operation = std::move(operation)](const render::detail::render_command_reporter&) {
          operation();
          return error::success;
        },
        render::detail::render_command_policy::once);
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
