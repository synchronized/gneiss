// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/render/detail/service_impl.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/logging/logger.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <limits>
#include <mutex>
namespace gneiss::render {
struct capture_request::state {
  std::mutex mutex;
  bool finished{};
  bool consumed{};
  error status{error::not_ready};
  frame_image image;
};
bool capture_request::ready() const {
  if (!state_)
    return false;
  std::lock_guard lock(state_->mutex);
  return state_->finished;
}
void capture_request::cancel() const {
  if (!state_)
    return;
  std::lock_guard lock(state_->mutex);
  if (state_->finished)
    return;
  state_->finished = true;
  state_->status = error::cancelled;
}
result<frame_image> capture_request::take() {
  if (!state_)
    return error::invalid_state;
  std::lock_guard lock(state_->mutex);
  if (state_->consumed)
    return error::invalid_state;
  if (!state_->finished)
    return error::not_ready;
  if (state_->status.failed())
    return state_->status;
  state_->consumed = true;
  return std::move(state_->image);
}
struct pick_request::state {
  std::mutex mutex;
  bool finished{};
  bool consumed{};
  error status{error::not_ready};
  pick_result value;
};
bool pick_request::ready() const {
  if (!state_)
    return false;
  std::lock_guard lock(state_->mutex);
  return state_->finished;
}
void pick_request::cancel() const {
  if (!state_)
    return;
  std::lock_guard lock(state_->mutex);
  if (state_->finished)
    return;
  state_->finished = true;
  state_->status = error::cancelled;
}
result<pick_result> pick_request::take() {
  if (!state_)
    return error::invalid_state;
  std::lock_guard lock(state_->mutex);
  if (state_->consumed)
    return error::invalid_state;
  if (!state_->finished)
    return error::not_ready;
  if (state_->status.failed())
    return state_->status;
  state_->consumed = true;
  return state_->value;
}
namespace {
void log_shutdown(const queue_stats& render_stats, const device_stats& stats,
                  error shutdown_result) {
  std::array<char, 768> performance_message{};
  const auto performance_written = std::snprintf(
      performance_message.data(), performance_message.size(),
      "渲染线程统计：提交帧=%llu，执行帧=%llu，替换帧=%llu，提交命令=%llu，执行命令=%llu，"
      "拒绝命令=%llu，跳过构造=%llu，队列高水位=%zu，最近构造=%.3f ms，复制=%zu bytes，"
      "最近排队=%.3f ms，最大排队=%.3f ms，GPU计时=%s，GPU样本=%llu，GPU暂不可用=%llu，"
      "最近GPU样本=%llu/%.3f ms（阴影=%.3f，不透明=%.3f，色调映射=%.3f），"
      "环境=%s，强度=%.3f，旋转=%.3f rad",
      static_cast<unsigned long long>(render_stats.submitted_frames),
      static_cast<unsigned long long>(render_stats.executed_frames),
      static_cast<unsigned long long>(render_stats.replaced_frames),
      static_cast<unsigned long long>(render_stats.submitted_commands),
      static_cast<unsigned long long>(render_stats.executed_commands),
      static_cast<unsigned long long>(render_stats.rejected_commands),
      static_cast<unsigned long long>(render_stats.skipped_frame_builds),
      render_stats.pending_high_watermark, render_stats.latest_frame_capture_ms,
      render_stats.latest_copied_payload_bytes, render_stats.latest_frame_queue_wait_ms,
      render_stats.maximum_frame_queue_wait_ms,
      render_stats.gpu_timing_supported ? "支持" : "不可用",
      static_cast<unsigned long long>(render_stats.gpu_timing_sample_count),
      static_cast<unsigned long long>(render_stats.gpu_timing_unavailable_count),
      static_cast<unsigned long long>(render_stats.latest_gpu_timing_sample_sequence),
      render_stats.latest_gpu_timing_valid ? render_stats.latest_gpu_frame_ms : 0.0F,
      render_stats.latest_gpu_shadow_ms, render_stats.latest_gpu_opaque_ms,
      render_stats.latest_gpu_tone_mapping_ms,
      render_stats.environment_fallback
          ? "资产失败后回退"
          : (render_stats.environment_asset_requested ? "工程资产" : "内建"),
      render_stats.environment_intensity, render_stats.environment_rotation_radians);
  const auto performance_length =
      performance_written > 0 ? std::min<std::size_t>(static_cast<std::size_t>(performance_written),
                                                      performance_message.size() - 1U)
                              : 0U;
  logging::logger{"render"}.write(
      {.severity = logging::level::info,
       .source = "granit.render.performance",
       .category = "backend",
       .message = std::string_view(performance_message.data(), performance_length)});
  if (shutdown_result != gneiss::error::success) {
    std::array<char, 768> message{};
    const auto written =
        stats.total_live_count == 0U
            ? std::snprintf(message.data(), message.size(),
                            "Granit GPU 逻辑资源退出检查失败，无法取得资源统计")
            : std::snprintf(
                  message.data(), message.size(),
                  "Granit 关闭前仍有 GPU 逻辑资源：总数=%llu，Buffer=%llu，Texture=%llu，"
                  "TextureView=%llu，Sampler=%llu，Shader=%llu，BindGroupLayout=%llu，"
                  "BindGroup=%llu，PipelineLayout=%llu，GraphicsPipeline=%llu，"
                  "ComputePipeline=%llu，Surface=%llu，Swapchain=%llu，CommandRecorder=%llu，"
                  "FrameContext=%llu，Frame=%llu，TimestampQueryPool=%llu，UploadBatch=%llu；"
                  "后端待回收=%llu",
                  static_cast<unsigned long long>(stats.total_live_count),
                  static_cast<unsigned long long>(stats.buffer_count),
                  static_cast<unsigned long long>(stats.texture_count),
                  static_cast<unsigned long long>(stats.texture_view_count),
                  static_cast<unsigned long long>(stats.sampler_count),
                  static_cast<unsigned long long>(stats.shader_count),
                  static_cast<unsigned long long>(stats.bind_group_layout_count),
                  static_cast<unsigned long long>(stats.bind_group_count),
                  static_cast<unsigned long long>(stats.pipeline_layout_count),
                  static_cast<unsigned long long>(stats.graphics_pipeline_count),
                  static_cast<unsigned long long>(stats.compute_pipeline_count),
                  static_cast<unsigned long long>(stats.surface_count),
                  static_cast<unsigned long long>(stats.swapchain_count),
                  static_cast<unsigned long long>(stats.command_recorder_count),
                  static_cast<unsigned long long>(stats.frame_context_count),
                  static_cast<unsigned long long>(stats.frame_count),
                  static_cast<unsigned long long>(stats.timestamp_query_pool_count),
                  static_cast<unsigned long long>(stats.upload_batch_count),
                  static_cast<unsigned long long>(stats.pending_retirement_count));
    const auto length =
        written > 0 ? std::min<std::size_t>(static_cast<std::size_t>(written), message.size() - 1U)
                    : 0U;
    logging::logger{"render"}.write({.severity = logging::level::error,
                                     .source = "granit.render.resources",
                                     .category = "backend",
                                     .message = std::string_view(message.data(), length),
                                     .result = shutdown_result});
  }
}
}
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
                                                      frame_policy policy, std::size_t input_bytes,
                                                      std::function<void(error)> completed) {
  const auto completion_result = collect_completions();
  if (completion_result != gneiss::error::success) {
    return completion_result;
  }
  return executor_.submit_frame({}, policy, std::move(prepare), input_bytes, std::move(completed));
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
  const auto render_stats = query_performance_stats();
  executor_.stop();
  log_shutdown(render_stats, stats, completion ? completion->status : submit_result.error());
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
result<capture_request> service::request_capture(render::frame_request&& request,
                                                 target_description capture_size) {
  if (bool(capture_size.width) != bool(capture_size.height))
    return error::invalid_argument;
  auto state = std::make_shared<capture_request::state>();
  auto image = std::make_shared<frame_image>();
  auto accepted = submit_request(request, frame_policy::required, image, capture_size,
                                 [state, image](error status) {
                                   std::lock_guard lock(state->mutex);
                                   if (state->finished)
                                     return;
                                   state->status = status;
                                   if (status.ok())
                                     state->image = std::move(*image);
                                   state->finished = true;
                                 });
  if (!accepted)
    return accepted.error();
  return capture_request{std::move(state)};
}
result<pick_request> service::request_pick(const view& input, target_description size,
                                           std::uint32_t x, std::uint32_t y) {
  if (!size.width || !size.height || x >= size.width || y >= size.height)
    return error::invalid_argument;
  frame_request frame;
  frame.surface.width = size.width;
  frame.surface.height = size.height;
  frame.views.push_back(input);
  // 拾取创建自己的离屏附件，不读写调用方的颜色目标。
  frame.views.back().target = {};
  auto state = std::make_shared<pick_request::state>();
  auto readback = std::make_shared<detail::pick_readback>();
  readback->x = x;
  readback->y = y;
  readback->result.scene = input.scene;
  auto accepted = submit_request(
      frame, frame_policy::required, {}, {},
      [state, readback](error status) {
        std::lock_guard lock(state->mutex);
        if (state->finished)
          return;
        state->status = status;
        if (status.ok())
          state->value = readback->result;
        state->finished = true;
      },
      readback);
  if (!accepted)
    return accepted.error();
  return pick_request{std::move(state)};
}
result<std::uint64_t> service::submit_request(render::frame_request& request, frame_policy policy,
                                              std::shared_ptr<frame_image> image,
                                              target_description capture_size,
                                              std::function<void(error)> completed,
                                              std::shared_ptr<detail::pick_readback> pick) {
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
       image = std::move(image),
       pick = std::move(pick)]() mutable -> result<gneiss::render::detail::frame_packet> {
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
        packet->pick = std::move(pick);
        return packet;
      },
      policy, input_bytes, std::move(completed));
  if (!accepted)
    return accepted.error();
  request = {};
  return *accepted;
}
queue_stats service::query_performance_stats() const noexcept {
  return impl_->query_performance_stats();
}
texture_prepare_profile service::texture_profile() const noexcept {
  return impl_->texture_profile();
}

} // namespace gneiss::render
