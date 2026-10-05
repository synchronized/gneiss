// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_state.hpp"

#include "asset/native_file_system.h"
#include "world/render_snapshot.hpp"

#ifdef GNEISS_HAS_GRANIT_PLATFORM
#include "platform/granit/granit_platform.h"
#include "render/backend/granit/granit_render_service.hpp"
#endif

#include <chrono>
#include <cstdio>
#include <new>
#include <vector>

namespace gneiss::application_internal {

application_state::application_state(const gneiss_application_desc& desc) noexcept
    : desc_(desc), owner_thread_(std::this_thread::get_id()) {}

application_state::~application_state() noexcept {
  static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
}

render_internal::render_queue_stats application_state::render_statistics() const noexcept {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_render_service_) {
    return granit_render_service_->query_performance_stats();
  }
#endif
  return {};
}

gneiss_result application_state::attach_task_executor(tasks::task_executor& executor) noexcept {
  if (texture_service_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
    asset_internal::texture_upload_backend backend;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    if (granit_render_service_) {
      backend.begin = [this](auto data, auto& sequence) {
        return granit_render_service_->prepare_textures(std::move(data), sequence);
      };
      backend.poll = [this](auto sequence, auto& result) {
        return granit_render_service_->poll_texture_preparation(sequence, result);
      };
      backend.discard = [this](auto data, auto& sequence) {
        return granit_render_service_->discard_prepared_textures(std::move(data), sequence);
      };
      backend.profile = granit_render_service_->texture_profile();
      backend.estimate_bytes = render_internal::granit_render_service::estimate_upload_bytes;
      backend.elapsed_ms = [this] { return granit_render_service_->latest_texture_upload_ms(); };
      backend.flush = [this] { (void)granit_render_service_->finish_frames(); };
    } else
#endif
    {
      // 无渲染平台的 Application 只验证 CPU 与资源事务，不宣称 GPU 上传成功。
      backend.begin = [](auto, auto& sequence) {
        sequence = 1U;
        return GNEISS_SUCCESS;
      };
      backend.poll = [](auto, auto& result) {
        result = GNEISS_SUCCESS;
        return true;
      };
      backend.discard = backend.begin;
      backend.flush = [] {};
    }
    scene_service_ =
        std::make_unique<scene_load_service>(executor, asset_file_system_, resources_, backend);
    texture_service_ = std::make_unique<asset_internal::texture_load_service>(
        executor, asset_file_system_, active_scene_->assets, std::move(backend));
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result application_state::activate_scene(std::uint64_t request,
                                                scene_load_completion& completion) {
  if (!scene_service_ || retired_scene_ || (texture_service_ && texture_service_->busy())) {
    return GNEISS_ERROR_NOT_READY;
  }
  const auto start = std::chrono::steady_clock::now();
  std::unique_ptr<asset_internal::texture_load_service> next_assets;
  auto next = scene_service_->take_candidate(request, completion, next_assets);
  if (!next) {
    return GNEISS_ERROR_NOT_READY;
  }
  retired_scene_ = std::move(active_scene_);
  active_scene_ = std::move(next);
  texture_service_ = std::move(next_assets);
  completion.activation_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  return GNEISS_SUCCESS;
}

gneiss_result application_state::reload_render_assets(
    std::span<const render_internal::render_asset_reload> assets) noexcept {
  if (scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  render_internal::asset_diagnostic diagnostic;
  return active_scene_->assets.reload_assets(assets, diagnostic);
}

gneiss_result application_state::reload_scene(gneiss_scene_instance instance,
                                              std::string_view uri) noexcept {
  if (scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return scenes() == nullptr ? GNEISS_ERROR_INVALID_STATE : scenes()->reload(instance, uri);
}

gneiss_result application_state::reload_prefab(gneiss_scene_instance instance,
                                               std::string_view uri) noexcept {
  if (scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return scenes() == nullptr ? GNEISS_ERROR_INVALID_STATE : scenes()->reload_prefab(instance, uri);
}

gneiss_result application_state::initialize() noexcept {
  if (desc_.log != nullptr) {
    try {
      log_dispatcher_ = std::make_unique<log_internal::log_dispatcher>(desc_.log, desc_.user_data);
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }
  if (!resources_.is_valid()) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  if (desc_.asset_root != nullptr || desc_.asset_root_length != 0U) {
    if (desc_.asset_root == nullptr || desc_.asset_root_length == 0U) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    std::shared_ptr<asset_internal::native_file_system> native_file_system;
    try {
      native_file_system = std::make_shared<asset_internal::native_file_system>();
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
    auto mount_result =
        native_file_system->initialize(std::string_view(desc_.asset_root, desc_.asset_root_length));
    if (mount_result == GNEISS_SUCCESS) {
      mount_result = asset_file_system_.mount("asset://", std::move(native_file_system));
    }
    if (mount_result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_ASSET,
             mount_result, "asset", "资产根目录挂载失败");
      return mount_result;
    }
  }
  if (desc_.platform == GNEISS_APPLICATION_PLATFORM_GRANIT) {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    try {
      granit_platform_ = std::make_unique<granit_platform>();
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
    const auto platform_result = granit_platform_->initialize(desc_);
    if (platform_result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
             platform_result, "granit.platform", "Granit 平台初始化失败");
      granit_platform_.reset();
      return platform_result;
    }
    try {
      granit_render_service_ = std::make_unique<render_internal::granit_render_service>();
    } catch (const std::bad_alloc&) {
      granit_platform_.reset();
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      granit_platform_.reset();
      return GNEISS_ERROR_INTERNAL;
    }
    std::vector<std::byte> environment_asset;
    if (desc_.environment_asset != nullptr) {
      const auto environment_result = asset_file_system_.read(
          std::string_view(desc_.environment_asset, desc_.environment_asset_length),
          environment_asset);
      if (environment_result != GNEISS_SUCCESS) {
        report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_ASSET,
               environment_result, "environment", "环境资产读取失败");
        granit_platform_.reset();
        return environment_result;
      }
    }
    const auto render_result = granit_render_service_->initialize(
        granit_platform_->native_window(), environment_asset, desc_.environment_intensity,
        desc_.environment_rotation_radians, log_dispatcher_.get());
    if (render_result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
             render_result, "granit.render", "Granit 渲染服务初始化失败");
      granit_render_service_.reset();
      granit_platform_.reset();
      return render_result;
    }
#else
    report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
           GNEISS_ERROR_UNSUPPORTED, "granit.platform", "当前构建未启用 Granit 平台适配");
    return GNEISS_ERROR_UNSUPPORTED;
#endif
  }
  if (desc_.initialize != nullptr) {
    const auto result = desc_.initialize(desc_.user_data);
    if (result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR,
             GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION, result, "application.initialize",
             "Application 初始化回调失败");
      // 初始化回调可能已获得部分资源，因此失败时也执行配对清理。
      platform_initialized_ = true;
      static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
      return result;
    }
  }
  platform_initialized_ = true;

  try {
    active_scene_ = std::make_unique<application_scene_state>(asset_file_system_, resources_);
    const auto result = active_scene_->initialize();
    if (result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR,
             GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION, result, "scene", "场景域创建失败");
      static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
      return result;
    }
  } catch (const std::bad_alloc&) {
    static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
    return GNEISS_ERROR_INTERNAL;
  }
  return GNEISS_SUCCESS;
}

std::uint64_t application_state::now_ns() const noexcept {
  if (desc_.now_ns != nullptr) {
    return desc_.now_ns(desc_.user_data);
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

gneiss_result application_state::poll_events(bool& out_should_close) noexcept {
  out_should_close = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_platform_ != nullptr) {
    input_.begin_frame();
    bool focus_lost = false;
    const auto platform_result = granit_platform_->poll(out_should_close, focus_lost);
    if (platform_result != GNEISS_SUCCESS) {
      return platform_result;
    }
    gneiss_keyboard_state keyboard = GNEISS_KEYBOARD_STATE_INIT;
    auto input_result = granit_platform_->keyboard(keyboard);
    if (input_result != GNEISS_SUCCESS) {
      return input_result;
    }
    gneiss_pointer_state pointer = GNEISS_POINTER_STATE_INIT;
    input_result = granit_platform_->pointer(pointer);
    if (input_result != GNEISS_SUCCESS) {
      return input_result;
    }
    input_.set_keyboard(keyboard);
    input_.set_pointer(pointer);
    gneiss_input_event event = GNEISS_INPUT_EVENT_INIT;
    input_result = granit_platform_->poll_input(event);
    while (input_result == GNEISS_SUCCESS) {
      if (!input_.push(event)) {
        input_.clear_focus();
        return GNEISS_ERROR_INVALID_STATE;
      }
      event = GNEISS_INPUT_EVENT_INIT;
      input_result = granit_platform_->poll_input(event);
    }
    if (focus_lost) {
      input_.clear_focus();
    }
    return input_result == GNEISS_ERROR_NOT_READY ? GNEISS_SUCCESS : input_result;
  }
#endif
  if (desc_.poll_events == nullptr) {
    return GNEISS_SUCCESS;
  }
  uint8_t should_close = 0;
  const auto result = desc_.poll_events(desc_.user_data, &should_close);
  out_should_close = should_close != 0U;
  return result;
}

gneiss_result application_state::poll_input(gneiss_input_event& out_event) noexcept {
  return input_.poll(out_event);
}

gneiss_result application_state::load_action_map(std::string_view uri) noexcept {
  input_internal::action_map map;
  const auto result = input_internal::load_action_map(asset_file_system_, uri, map);
  return result == GNEISS_SUCCESS ? input_.replace_action_map(std::move(map)) : result;
}

gneiss_result application_state::find_action(std::string_view name,
                                             gneiss_action& out_action) const noexcept {
  return input_.find_action(name, out_action);
}

gneiss_result application_state::get_action_state(gneiss_action action,
                                                  gneiss_action_state& out_state) const noexcept {
  return input_.get_action_state(action, out_state);
}

gneiss_result
application_state::submit_ui_draw_list(const gneiss_ui_draw_list_desc& desc) noexcept {
  return is_updating_ ? ui_draw_list_.replace(desc, resources_) : GNEISS_ERROR_INVALID_STATE;
}

gneiss_result
application_state::submit_debug_draw_list(const gneiss_debug_draw_list_desc& desc) noexcept {
  return is_updating_ ? debug_draw_list_.replace(desc) : GNEISS_ERROR_INVALID_STATE;
}

void application_state::report(gneiss_application handle, std::uint32_t severity,
                               std::uint32_t category, gneiss_result result,
                               std::string_view module, std::string_view message) noexcept {
  if (desc_.diagnostic != nullptr) {
    const gneiss_diagnostic diagnostic = {
        .struct_size = sizeof(gneiss_diagnostic),
        .severity = severity,
        .category = category,
        .result = result,
        .module = module.data(),
        .module_length = module.size(),
        .message = message.data(),
        .message_length = message.size(),
        .reserved = {},
    };
    desc_.diagnostic(handle, &diagnostic, desc_.user_data);
  }
  const auto log_severity = severity == GNEISS_DIAGNOSTIC_INFO      ? GNEISS_LOG_INFO
                            : severity == GNEISS_DIAGNOSTIC_WARNING ? GNEISS_LOG_WARNING
                                                                    : GNEISS_LOG_ERROR;
  const std::string_view log_category = category == GNEISS_DIAGNOSTIC_CATEGORY_ASSET   ? "asset"
                                        : category == GNEISS_DIAGNOSTIC_CATEGORY_INPUT ? "input"
                                        : category == GNEISS_DIAGNOSTIC_CATEGORY_BACKEND
                                            ? "backend"
                                            : "application";
  const gneiss_log_message log_message = {
      .struct_size = sizeof(gneiss_log_message),
      .severity = log_severity,
      .category = log_category.data(),
      .category_length = log_category.size(),
      .message = message.data(),
      .message_length = message.size(),
      .result = result,
      .flags = 0U,
      .reserved = {},
  };
  static_cast<void>(submit_log(handle, log_message, module));
}

gneiss_result application_state::submit_log(gneiss_application handle,
                                            const gneiss_log_message& message,
                                            std::string_view source) noexcept {
  return log_dispatcher_ == nullptr ? GNEISS_SUCCESS
                                    : log_dispatcher_->submit(handle, message, source);
}

gneiss_result application_state::capture_frame(std::uint32_t width, std::uint32_t height,
                                               render_internal::frame_image& output) noexcept {
  output = {};
  if (width == 0U || height == 0U || width > 1024U || height > 1024U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (!granit_render_service_) {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  try {
    auto window = granit_platform_->native_window();
    window.width = width;
    window.height = height;
    window.needs_recreate = false;
    render_internal::render_snapshot snapshot;
    auto result = world_internal::get_render_snapshot(world(), width, height, snapshot);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    render_internal::render_frame_packet packet;
    result = render_internal::capture_render_frame_packet(window, std::move(snapshot), resources_,
                                                          ui_draw_list_, debug_draw_list_, packet);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    auto image = std::make_shared<render_internal::frame_image>();
    packet.readback = image;
    std::uint64_t sequence{};
    result = granit_render_service_->submit(
        std::move(packet), render_internal::render_frame_policy::required, &sequence);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    result = granit_render_service_->finish_frames();
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    render_internal::render_frame_completion completed;
    if (!granit_render_service_->try_take_required_completion(completed) ||
        completed.sequence != sequence || completed.status != GNEISS_SUCCESS) {
      return GNEISS_ERROR_INTERNAL;
    }
    output = std::move(*image);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
#else
  return GNEISS_ERROR_UNSUPPORTED;
#endif
}

#ifdef GNEISS_HAS_GRANIT_PLATFORM
gneiss_result application_state::render_frame() noexcept {
  if (granit_render_service_ == nullptr) {
    return GNEISS_SUCCESS;
  }
  auto& window = granit_platform_->native_window();
  if (window.width == 0U || window.height == 0U) {
    return GNEISS_SUCCESS;
  }
  render_internal::render_frame_packet packet;
  bool should_prepare = false;
  const auto storage_result =
      granit_render_service_->prepare_frame_packet_storage(packet, should_prepare);
  if (storage_result != GNEISS_SUCCESS || !should_prepare) {
    return storage_result;
  }
  render_internal::render_snapshot snapshot;
  const auto snapshot_result =
      world_internal::get_render_snapshot(world(), window.width, window.height, snapshot);
  if (snapshot_result != GNEISS_SUCCESS) {
    return snapshot_result;
  }
  const auto capture_result = render_internal::capture_render_frame_packet(
      window, std::move(snapshot), resources_, ui_draw_list_, debug_draw_list_, packet);
  if (capture_result != GNEISS_SUCCESS) {
    return capture_result;
  }
  const auto requested_recreate = window.needs_recreate;
  window.needs_recreate = false;
  const auto submit_result = granit_render_service_->submit(std::move(packet));
  if (submit_result != GNEISS_SUCCESS && requested_recreate) {
    window.needs_recreate = true;
  }
  return submit_result;
}
#endif

gneiss_result application_state::get_window_size(std::uint32_t& out_width,
                                                 std::uint32_t& out_height) const noexcept {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_platform_ != nullptr) {
    const auto& window = granit_platform_->native_window();
    out_width = window.width;
    out_height = window.height;
    return GNEISS_SUCCESS;
  }
#endif
  out_width = desc_.window_width;
  out_height = desc_.window_height;
  return GNEISS_SUCCESS;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 句柄与帧数虽同宽但语义明确。
gneiss_result application_state::run(gneiss_application handle,
                                     std::uint64_t max_frame_count) noexcept {
  if (is_running_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  is_running_ = true;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_render_service_ != nullptr) {
    granit_render_service_->set_log_application(handle);
  }
#endif
  should_exit_ = false;
  previous_time_ns_ = now_ns();
  std::uint64_t frames_run = 0;

  while (!should_exit_ && (max_frame_count == 0U || frames_run < max_frame_count)) {
    bool should_close = false;
    const auto poll_result = poll_events(should_close);
    if (poll_result != GNEISS_SUCCESS) {
      is_running_ = false;
      return poll_result;
    }
    if (should_close) {
      if (desc_.close_requested == nullptr ||
          desc_.close_requested(handle, desc_.user_data) != 0U) {
        break;
      }
    }

    const auto current_time_ns = now_ns();
    const auto raw_delta =
        current_time_ns >= previous_time_ns_ ? current_time_ns - previous_time_ns_ : 0U;
    previous_time_ns_ = current_time_ns;
    const auto delta_ns = is_paused_ ? 0U : raw_delta;
    elapsed_ns_ += delta_ns;
    const gneiss_frame_time time = {
        .frame_index = frame_index_,
        .delta_ns = delta_ns,
        .elapsed_ns = elapsed_ns_,
        .is_paused = static_cast<std::uint8_t>(is_paused_ ? 1U : 0U),
        .reserved = {},
    };
    ui_draw_list_.clear();
    debug_draw_list_.clear();
    if (desc_.update != nullptr) {
      is_updating_ = true;
      const auto update_result = desc_.update(handle, &time, desc_.user_data);
      is_updating_ = false;
      if (update_result != GNEISS_SUCCESS) {
        ui_draw_list_.clear();
        debug_draw_list_.clear();
        is_running_ = false;
        return update_result;
      }
    }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    const auto render_result = render_frame();
    // 渲染队列、交换链或后端资源可能暂时未就绪；跳过本帧并在下一帧重试。
    if (render_result != GNEISS_SUCCESS && render_result != GNEISS_ERROR_NOT_READY) {
      ui_draw_list_.clear();
      debug_draw_list_.clear();
      is_running_ = false;
      return render_result;
    }
#endif
    ui_draw_list_.clear();
    debug_draw_list_.clear();
    ++frame_index_;
    ++frames_run;
  }

  is_running_ = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  // 返回前回收已提交帧，避免短程运行把尚未回传的上传失败误报为成功。
  if (granit_render_service_ != nullptr) {
    return granit_render_service_->finish_frames();
  }
#endif
  return GNEISS_SUCCESS;
}

bool application_state::is_owner_thread() const noexcept {
  return owner_thread_ == std::this_thread::get_id();
}

gneiss_result application_state::shutdown(gneiss_application handle) noexcept {
  auto shutdown_result = GNEISS_SUCCESS;
  scene_service_.reset();
  texture_service_.reset();
  retired_scene_.reset();
  is_updating_ = false;
  ui_draw_list_.clear();
  debug_draw_list_.clear();
  active_scene_.reset();
  if (platform_initialized_) {
    if (desc_.shutdown != nullptr) {
      desc_.shutdown(desc_.user_data);
    }
    platform_initialized_ = false;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_render_service_ != nullptr) {
    const auto render_stats = granit_render_service_->query_performance_stats();
    granit::renderer_resource_stats stats;
    shutdown_result = granit_render_service_->shutdown(stats);
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
        performance_written > 0
            ? std::min<std::size_t>(static_cast<std::size_t>(performance_written),
                                    performance_message.size() - 1U)
            : 0U;
    report(handle, GNEISS_DIAGNOSTIC_INFO, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND, GNEISS_SUCCESS,
           "granit.render.performance",
           std::string_view(performance_message.data(), performance_length));
    if (shutdown_result != GNEISS_SUCCESS) {
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
      const auto length = written > 0 ? std::min<std::size_t>(static_cast<std::size_t>(written),
                                                              message.size() - 1U)
                                      : 0U;
      report(handle, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND, shutdown_result,
             "granit.render.resources", std::string_view(message.data(), length));
    }
  }
  granit_render_service_.reset();
  granit_platform_.reset();
#else
  (void)handle;
#endif
  return shutdown_result;
}

} // namespace gneiss::application_internal
