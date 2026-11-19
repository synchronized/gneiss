// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/render/detail/service_impl.hpp"
#include <algorithm>
#include <cmath>
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <limits>
namespace gneiss::render {
gneiss::error service::implementation::initialize(const gneiss::window::native_handle& window,
                                                  window::surface_state surface,
                                                  std::span<const std::byte> environment_asset,
                                                  float environment_intensity,
                                                  float environment_rotation_radians,
                                                  queue_options queues) {
  const auto executor_result = executor_.initialize(
      [this](gneiss::render::detail::frame_packet& packet, execution_result& output) {
        return backend_.draw(packet, output);
      },
      queues);
  if (executor_result != gneiss::error::success) {
    return executor_result;
  }

  const auto submit_result = executor_.submit_command(
      [this, window, surface, environment_asset, environment_intensity,
       environment_rotation_radians](const render::detail::render_command_reporter& reporter) {
        reporter.report(render::detail::render_command_stage::uploading, 0U, 1U);
        const auto result =
            backend_.initialize(window, surface, environment_asset, environment_intensity,
                                environment_rotation_radians);
        reporter.report(render::detail::render_command_stage::uploading, 1U, 1U);
        return result;
      });
  if (submit_result.ok()) {
    static_cast<void>(executor_.flush());
  }

  auto completion = executor_.try_take_command_completion();
  // 新执行器只有这条命令，flush 在回执发布及捕获数据释放之后才返回。
  if (submit_result) {
    GNEISS_ASSERT(completion.has_value(), "初始化命令缺少完成回执");
    GNEISS_ASSERT(completion->sequence == *submit_result, "初始化命令回执身份不匹配");
  }
  const auto initialize_result = submit_result ? completion->status : submit_result.error();
  if (initialize_result.failed()) {
    device_stats ignored{};
    if (executor_
            .submit_command([this, &ignored](const render::detail::render_command_reporter&) {
              return backend_.shutdown(ignored);
            })
            .ok()) {
      static_cast<void>(executor_.flush());
    }
    executor_.stop();
    return initialize_result;
  }
  return initialize_result;
}

error service::implementation::request_environment(
    std::shared_ptr<const std::vector<std::byte>> bytes, float intensity, float rotation) {
  if ((bytes && bytes->empty()) || !std::isfinite(intensity) || intensity < 0.0F ||
      !std::isfinite(rotation))
    return error::invalid_argument;
  const auto payload_bytes = bytes ? bytes->size() : 0;
  const std::weak_ptr<const void> payload = bytes;
  auto submitted = executor_.submit_command(
      [this, bytes = std::move(bytes), intensity,
       rotation](const render::detail::render_command_reporter&) {
        return backend_.set_environment(bytes ? std::span<const std::byte>(*bytes)
                                              : std::span<const std::byte>{},
                                        intensity, rotation);
      },
      render::detail::render_command_policy::once, payload_bytes, payload);
  return submitted ? error::success : submitted.error();
}

gneiss::error service::implementation::collect_completions() {
  while (auto command = executor_.try_take_command_completion()) {
    if (command->status.failed())
      return command->status;
  }
  while (auto completion = executor_.try_take_frame_completion()) {
    if (completion->policy == frame_policy::required) {
      required_frame_completions_.push_back({.sequence = completion->sequence,
                                             .status = completion->status,
                                             .execution = completion->execution,
                                             .dropped = completion->dropped,
                                             .policy = completion->policy});
    }
    if (completion->dropped) {
      continue;
    }
    if (completion->status != gneiss::error::success) {
      return completion->status;
    }
  }
  return gneiss::error::success;
}

result<bool> service::implementation::can_prepare_frame() {
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
result<std::uint64_t> service::implementation::submit(render::detail::render_frame_prepare prepare,
                                                      frame_policy policy,
                                                      std::size_t input_bytes) {
  const auto completion_result = collect_completions();
  if (completion_result != gneiss::error::success) {
    return completion_result;
  }
  return executor_.submit_frame({}, policy, std::move(prepare), input_bytes);
}

result<std::optional<frame_completion>> service::implementation::try_take_required_completion() {
  const auto status = collect_completions();
  if (status.failed())
    return status;
  if (required_frame_completions_.empty())
    return std::optional<frame_completion>{};
  auto completion = std::move(required_frame_completions_.front());
  required_frame_completions_.pop_front();
  return std::optional<frame_completion>{std::move(completion)};
}

queue_stats service::implementation::query_performance_stats() const noexcept {
  return executor_.query_stats();
}

gneiss::error service::implementation::finish_frames() {
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

gneiss::error service::implementation::shutdown(device_stats& stats) {
  if (!executor_.is_running()) {
    stats = {};
    return gneiss::error::success;
  }
  // 排空所有旧回执后再提交关闭命令，避免把历史失败后的回执误认成关闭结果。
  const auto frame_result = finish_frames();

  const auto submit_result = executor_.submit_command(
      [this, &stats](const render::detail::render_command_reporter& reporter) {
        reporter.report(render::detail::render_command_stage::uploading, 0U, 1U);
        const auto result = backend_.shutdown(stats);
        reporter.report(render::detail::render_command_stage::uploading, 1U, 1U);
        return result;
      });
  if (submit_result.ok()) {
    static_cast<void>(executor_.flush());
  }
  auto completion = executor_.try_take_command_completion();
  // 所属线程已排空旧回执，期间没有其他服务调用者提交或领取关闭回执。
  if (submit_result) {
    GNEISS_ASSERT(completion.has_value(), "关闭命令缺少完成回执");
    GNEISS_ASSERT(completion->sequence == *submit_result, "关闭命令回执身份不匹配");
  }
  executor_.stop();
  required_frame_completions_.clear();
  if (frame_result != gneiss::error::success) {
    return frame_result;
  }
  if (!submit_result) {
    return submit_result.error();
  }
  return completion->status;
}

service::service(std::uint64_t memory_limit)
    : impl_(std::make_unique<implementation>(memory_limit)) {}
result<void> service::dispatch_render_work(std::function<void()> operation, std::size_t bytes,
                                           std::weak_ptr<const void> payload) {
  return impl_->dispatch(std::move(operation), bytes, std::move(payload));
}
service::~service() = default;
error service::request_environment(std::shared_ptr<const std::vector<std::byte>> bytes,
                                   float intensity, float rotation) {
  return impl_->request_environment(std::move(bytes), intensity, rotation);
}

error service::initialize(const gneiss::window::native_handle& window,
                          window::surface_state surface,
                          std::span<const std::byte> environment_asset, float environment_intensity,
                          float environment_rotation_radians, queue_options queues) {
  if (!is_valid())
    return error::invalid_state;
  return impl_->initialize(window, surface, environment_asset, environment_intensity,
                           environment_rotation_radians, queues);
}
error service::shutdown(device_stats& stats) {
  const auto status = impl_->shutdown(stats);
  // shutdown 已停止执行器；历史帧错误不应使 CPU 入口继续接受新资源和场景。
  close_resources();
  return status;
}

error service::finish_frames() { return impl_->finish_frames(); }
void service::set_completion_wakeup(std::shared_ptr<wakeup_signal> notification) noexcept {
  impl_->set_completion_wakeup(std::move(notification));
}
result<void> service::render(render::frame_request&& request) {
  auto ready = impl_->can_prepare_frame();
  if (!ready)
    return ready.error();
  if (!*ready)
    return error::not_ready;
  auto submitted = submit_request(request, frame_policy::replaceable, {});
  return submitted ? result<void>{} : result<void>{submitted.error()};
}
result<std::uint64_t> service::request_capture(render::frame_request&& request,
                                               std::shared_ptr<frame_image> image,
                                               target_description capture_size) {
  if (!image || bool(capture_size.width) != bool(capture_size.height))
    return error::invalid_argument;
  return submit_request(request, frame_policy::required, std::move(image), capture_size);
}
result<std::uint64_t> service::submit_request(render::frame_request& request, frame_policy policy,
                                              std::shared_ptr<frame_image> image,
                                              target_description capture_size) {
  if (image && request.views.size() > 1 &&
      std::ranges::none_of(request.views, [](const auto& view) { return !view.target; }))
    return error::unsupported; // 多视图截图明确返回窗口合成结果。
  std::vector<target_description> sizes;
  sizes.reserve(request.views.size());
  if (request.views.empty() && image && capture_size.width && capture_size.height)
    sizes.push_back(capture_size);
  for (const auto& view : request.views) {
    if (!view.target) {
      sizes.push_back(image && capture_size.width && capture_size.height
                          ? capture_size
                          : target_description{request.surface.width, request.surface.height});
      continue;
    }
    const auto* description = impl_->targets.get(view.target, resource_type::render_target);
    if (!description)
      return error::invalid_handle;
    sizes.push_back(*description);
  }
  auto capture = impl_->prepare_capture(request, std::move(sizes));
  if (!capture)
    return capture.error();
  // UI 是独立逐帧输入，接受时固定其纹理版本，不在消费者访问资源 RID 表。
  gneiss::render::detail::resource_snapshot ui_resources;
  for (const auto& command : request.ui.commands()) {
    if (const auto* target = std::get_if<ui_target_image>(&command.texture)) {
      if (!is_target(target->target))
        return error::invalid_handle;
      const bool produced = std::ranges::any_of(
          request.views, [&](const auto& view) { return view.target == target->target; });
      const bool window_output =
          request.views.empty() ||
          std::ranges::any_of(request.views, [](const auto& view) { return !view.target; });
      if (!produced || !window_output)
        return error::invalid_argument;
      continue;
    }
    const auto handle = resource_handle(std::get<ref<const texture>>(command.texture));
    const auto status = texture_status(handle);
    if (status.failed())
      return status;
    auto texture = share_texture(handle);
    if (!texture)
      return error::invalid_handle;
    ui_resources.textures_.insert_or_assign(handle, std::move(texture));
  }
  // 只计队列直接复制的逐帧输入，不递归计算资源/场景，生成的绘制快照另属执行阶段。
  std::size_t input_bytes{};
  const auto account = [&](std::size_t count, std::size_t element) {
    const auto available = std::numeric_limits<std::size_t>::max() - input_bytes;
    if (count > available / element)
      return false;
    input_bytes += count * element;
    return true;
  };
  if (!account(request.views.size(), sizeof(view)) ||
      !account(request.ui.vertices().size(), sizeof(ui_vertex)) ||
      !account(request.ui.indices().size(), sizeof(std::uint32_t)) ||
      !account(request.ui.commands().size(), sizeof(ui_draw_command)) ||
      !account(request.debug.lines().size(), sizeof(debug_line)))
    return error::out_of_memory;
  auto accepted = impl_->submit(
      [capture = std::move(*capture), ui_resources = std::move(ui_resources),
       image = std::move(image)]() mutable -> result<gneiss::render::detail::frame_packet> {
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
      policy, input_bytes);
  if (!accepted)
    return accepted.error();
  request = {};
  return *accepted;
}
result<std::optional<frame_completion>> service::try_take_required_completion() {
  return impl_->try_take_required_completion();
}
queue_stats service::query_performance_stats() const noexcept {
  return impl_->query_performance_stats();
}
texture_prepare_profile service::texture_profile() const noexcept {
  return impl_->texture_profile();
}

} // namespace gneiss::render
