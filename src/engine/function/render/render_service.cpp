// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render_service.hpp"
#include "engine/function/render/detail/service_impl.hpp"
#include <cmath>
namespace gneiss {
gneiss::error render_service::implementation::initialize(
    const native_window_info& window, std::span<const std::byte> environment_asset,
    float environment_intensity, float environment_rotation_radians) {
  const auto executor_result =
      executor_.initialize([this](render_frame_packet& packet, render_execution_result& output) {
        const auto result = backend_.draw(packet, output);
        output.needs_recreate = packet.window.needs_recreate;
        return result;
      });
  if (executor_result != gneiss::error::success) {
    return executor_result;
  }

  gneiss::error initialize_result = gneiss::error::unknown;
  const auto submit_result = executor_.submit_command(
      [this, window, environment_asset, environment_intensity, environment_rotation_radians,
       &initialize_result](const render_command_reporter& reporter) {
        reporter.report(render_command_stage::uploading, 0U, 1U);
        initialize_result = backend_.initialize(window, environment_asset, environment_intensity,
                                                environment_rotation_radians);
        reporter.report(render_command_stage::uploading, 1U, 1U);
        return initialize_result;
      });
  if (submit_result.ok()) {
    static_cast<void>(executor_.flush());
  }

  auto completion = executor_.try_take_command_completion();
  if (!submit_result || !completion || completion->sequence != *submit_result ||
      completion->status != gneiss::error::success) {
    render_device_stats ignored{};
    if (executor_
            .submit_command([this, &ignored](const render_command_reporter&) {
              return backend_.shutdown(ignored);
            })
            .ok()) {
      static_cast<void>(executor_.flush());
    }
    executor_.stop();
    return !submit_result ? submit_result.error()
                          : (completion ? completion->status : gneiss::error::internal);
  }
  return initialize_result;
}

error render_service::implementation::request_environment(
    std::shared_ptr<const std::vector<std::byte>> bytes, float intensity, float rotation) {
  if (!bytes || bytes->empty() || !std::isfinite(intensity) || !std::isfinite(rotation))
    return error::invalid_argument;
  auto submitted = executor_.submit_command(
      [this, bytes = std::move(bytes), intensity, rotation](const render_command_reporter&) {
        return backend_.set_environment(*bytes, intensity, rotation);
      });
  return submitted ? error::success : submitted.error();
}

gneiss::error render_service::implementation::collect_completions() {
  while (auto command = executor_.try_take_command_completion()) {
    if (command->status.failed())
      return command->status;
  }
  while (auto completion = executor_.try_take_frame_completion()) {
    if (completion->policy == render_frame_policy::required) {
      required_frame_completions_.push_back({.sequence = completion->sequence,
                                             .status = completion->status,
                                             .execution = completion->execution,
                                             .dropped = completion->dropped,
                                             .policy = completion->policy});
    }
    if (completion->dropped) {
      continue;
    }
    pending_recreate_ = pending_recreate_ || completion->execution.needs_recreate;
    if (completion->status != gneiss::error::success) {
      return completion->status;
    }
  }
  return gneiss::error::success;
}

result<bool> render_service::implementation::can_prepare_frame() {
  const auto completion_result = collect_completions();
  if (completion_result != gneiss::error::success) {
    return completion_result;
  }
  // 交互帧只预备一帧：低帧率时积压三份旧相机快照会把输入延迟放大到数百毫秒。
  // 保留一份等待帧与正在执行的帧重叠；队列满时继续处理输入，空位出现后再捕获最新状态。
  const bool should_prepare = executor_.query_stats().pending_frames < 1U;
  if (!should_prepare) {
    executor_.record_skipped_frame_build();
    return false;
  }
  return true;
}
result<std::uint64_t> render_service::implementation::submit(render_frame_prepare prepare,
                                                             render_frame_policy policy) {
  const auto completion_result = collect_completions();
  if (completion_result != gneiss::error::success) {
    return completion_result;
  }
  const bool recreate = pending_recreate_;
  auto accepted = executor_.submit_frame(
      {}, policy,
      [prepare = std::move(prepare), recreate]() mutable -> result<render_frame_packet> {
        auto packet = prepare();
        if (packet)
          packet->window.needs_recreate = packet->window.needs_recreate || recreate;
        return packet;
      });
  if (accepted)
    pending_recreate_ = false;
  return accepted;
}

result<std::optional<render_frame_completion>>
render_service::implementation::try_take_required_completion() {
  const auto status = collect_completions();
  if (status.failed())
    return status;
  if (required_frame_completions_.empty())
    return std::optional<render_frame_completion>{};
  auto completion = std::move(required_frame_completions_.front());
  required_frame_completions_.pop_front();
  return std::optional<render_frame_completion>{std::move(completion)};
}

render_queue_stats render_service::implementation::query_performance_stats() const noexcept {
  return executor_.query_stats();
}

gneiss::error render_service::implementation::finish_frames() {
  const auto flushed = executor_.flush();
  if (flushed != gneiss::error::success) {
    return flushed;
  }
  auto result = gneiss::error::success;
  for (auto completed = collect_completions(); completed != gneiss::error::success;
       completed = collect_completions()) {
    if (result == gneiss::error::success && completed != gneiss::error::not_ready) {
      result = completed;
    }
  }
  return result;
}

gneiss::error render_service::implementation::shutdown(render_device_stats& stats) {
  if (!executor_.is_running()) {
    stats = {};
    return gneiss::error::success;
  }
  static_cast<void>(executor_.flush());
  const auto frame_result = collect_completions();

  const auto submit_result =
      executor_.submit_command([this, &stats](const render_command_reporter& reporter) {
        reporter.report(render_command_stage::uploading, 0U, 1U);
        const auto result = backend_.shutdown(stats);
        reporter.report(render_command_stage::uploading, 1U, 1U);
        return result;
      });
  if (submit_result.ok()) {
    static_cast<void>(executor_.flush());
  }
  auto completion = executor_.try_take_command_completion();
  executor_.stop();
  required_frame_completions_.clear();
  if (frame_result != gneiss::error::success) {
    return frame_result;
  }
  if (!submit_result) {
    return submit_result.error();
  }
  if (!completion || completion->sequence != *submit_result) {
    return gneiss::error::internal;
  }
  return completion->status;
}

render_service::render_service(std::uint64_t memory_limit)
    : impl_(std::make_unique<implementation>(memory_limit)) {}
result<void> render_service::dispatch_render_work(std::function<void()> operation) {
  return impl_->dispatch(std::move(operation));
}
render_service::~render_service() = default;
error render_service::request_environment(std::shared_ptr<const std::vector<std::byte>> bytes,
                                          float intensity, float rotation) {
  return impl_->request_environment(std::move(bytes), intensity, rotation);
}

error render_service::initialize(const native_window_info& window,
                                 std::span<const std::byte> environment_asset,
                                 float environment_intensity, float environment_rotation_radians) {
  if (!is_valid())
    return error::invalid_state;
  return impl_->initialize(window, environment_asset, environment_intensity,
                           environment_rotation_radians);
}
error render_service::shutdown(render_device_stats& stats) {
  const auto status = impl_->shutdown(stats);
  if (status.ok())
    close_resources();
  return status;
}

error render_service::finish_frames() { return impl_->finish_frames(); }
void render_service::set_completion_wakeup(std::shared_ptr<wakeup_signal> notification) noexcept {
  impl_->set_completion_wakeup(std::move(notification));
}
result<void> render_service::render(render::frame_request&& request) {
  auto ready = impl_->can_prepare_frame();
  if (!ready)
    return ready.error();
  if (!*ready)
    return error::not_ready;
  auto submitted = submit_request(request, render_frame_policy::replaceable, {});
  return submitted ? result<void>{} : result<void>{submitted.error()};
}
result<std::uint64_t> render_service::request_capture(render::frame_request&& request,
                                                      std::shared_ptr<frame_image> image) {
  if (!image)
    return error::invalid_argument;
  return submit_request(request, render_frame_policy::required, std::move(image));
}
result<std::uint64_t> render_service::submit_request(render::frame_request& request,
                                                     render_frame_policy policy,
                                                     std::shared_ptr<frame_image> image) {
  auto capture = impl_->scenes.prepare_capture(request);
  if (!capture)
    return capture.error();
  // UI 是独立逐帧输入，接受时固定其纹理版本，不在消费者访问资源 RID 表。
  render_resource_snapshot ui_resources;
  for (const auto& command : request.ui.commands()) {
    const auto handle = resource_handle(command.texture);
    const auto status = texture_status(handle);
    if (status.failed())
      return status;
    auto texture = share_texture(handle);
    if (!texture)
      return error::invalid_handle;
    ui_resources.textures_.insert_or_assign(handle, std::move(texture));
  }
  auto accepted = impl_->submit(
      [capture = std::move(*capture), ui_resources = std::move(ui_resources),
       image = std::move(image)]() mutable -> result<render_frame_packet> {
        auto packet = capture();
        if (!packet)
          return packet.error();
        for (auto& [handle, texture] : ui_resources.textures_)
          packet->resources.textures_.insert_or_assign(handle, std::move(texture));
        packet->capture.copied_payload_bytes +=
            packet->ui.vertices().size() * sizeof(ui_vertex) +
            packet->ui.indices().size() * sizeof(std::uint32_t) +
            packet->ui.commands().size() * sizeof(ui_draw_command) +
            packet->debug.lines().size() * sizeof(debug_line);
        packet->readback = std::move(image);
        return packet;
      },
      policy);
  if (!accepted)
    return accepted.error();
  request = {};
  return *accepted;
}
result<std::optional<render_frame_completion>> render_service::try_take_required_completion() {
  return impl_->try_take_required_completion();
}
render_queue_stats render_service::query_performance_stats() const noexcept {
  return impl_->query_performance_stats();
}
texture_prepare_profile render_service::texture_profile() const noexcept {
  return impl_->texture_profile();
}

} // namespace gneiss
