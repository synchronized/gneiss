// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_state.h"

#include "asset/native_file_system.h"

#ifdef GNEISS_HAS_GRANIT_PLATFORM
#include "platform/granit/granit_platform.h"
#include "render/granit/granit_render_service.h"
#endif

#include <chrono>
#include <cstdio>
#include <new>

namespace gneiss::application_internal {

application_state::application_state(const gneiss_application_desc& desc) noexcept
    : desc_(desc), asset_loader_(asset_file_system_, asset_cache_, resources_),
      prefab_asset_loader_(asset_file_system_, asset_cache_),
      owner_thread_(std::this_thread::get_id()) {}

application_state::~application_state() noexcept {
  static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
}

gneiss_result application_state::reload_render_assets(
    std::span<const render_internal::render_asset_reload> assets) noexcept {
  render_internal::asset_diagnostic diagnostic;
  return asset_loader_.reload_assets(assets, diagnostic);
}

gneiss_result application_state::reload_scene(gneiss_scene_instance instance,
                                              std::string_view uri) noexcept {
  return scenes_ == nullptr ? GNEISS_ERROR_INVALID_STATE : scenes_->reload(instance, uri);
}

gneiss_result application_state::reload_prefab(gneiss_scene_instance instance,
                                               std::string_view uri) noexcept {
  return scenes_ == nullptr ? GNEISS_ERROR_INVALID_STATE : scenes_->reload_prefab(instance, uri);
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
      granit_render_service_ = std::make_unique<granit_render_service>();
    } catch (const std::bad_alloc&) {
      granit_platform_.reset();
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      granit_platform_.reset();
      return GNEISS_ERROR_INTERNAL;
    }
    const auto render_result =
        granit_render_service_->initialize(granit_platform_->native_window());
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

  const gneiss_world_desc world_desc = GNEISS_WORLD_DESC_INIT;
  const auto result = gneiss_world_create(&world_desc, &world_);
  if (result != GNEISS_SUCCESS) {
    report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION,
           result, "world", "World 创建失败");
    static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
    return result;
  }
  try {
    scenes_ = std::make_unique<scene_internal::scene_instance_service>(
        world_, asset_file_system_, asset_loader_, prefab_asset_loader_);
  } catch (const std::bad_alloc&) {
    static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
    return GNEISS_ERROR_INTERNAL;
  }
  if (!scenes_->is_valid()) {
    report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION,
           GNEISS_ERROR_OUT_OF_MEMORY, "scene", "Scene Instance Service 创建失败");
    static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
    return GNEISS_ERROR_OUT_OF_MEMORY;
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

#ifdef GNEISS_HAS_GRANIT_PLATFORM
gneiss_result application_state::render_frame() noexcept {
  if (granit_render_service_ == nullptr) {
    return GNEISS_SUCCESS;
  }
  world_internal::render_snapshot snapshot;
  auto& window = granit_platform_->native_window();
  if (window.width == 0U || window.height == 0U) {
    return GNEISS_SUCCESS;
  }
  const auto snapshot_result =
      world_internal::get_render_snapshot(world_, window.width, window.height, snapshot);
  if (snapshot_result != GNEISS_SUCCESS) {
    return snapshot_result;
  }
  render_internal::render_frame_packet packet;
  const auto capture_result = render_internal::capture_render_frame_packet(
      window, std::move(snapshot), resources_, ui_draw_list_, debug_draw_list_, packet);
  if (capture_result != GNEISS_SUCCESS) {
    return capture_result;
  }
  const auto render_result = granit_render_service_->render(packet);
  window.needs_recreate = window.needs_recreate || packet.window.needs_recreate;
  return render_result;
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
    if (render_result != GNEISS_SUCCESS) {
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
  return GNEISS_SUCCESS;
}

bool application_state::is_owner_thread() const noexcept {
  return owner_thread_ == std::this_thread::get_id();
}

gneiss_result application_state::shutdown(gneiss_application handle) noexcept {
  auto shutdown_result = GNEISS_SUCCESS;
  is_updating_ = false;
  ui_draw_list_.clear();
  debug_draw_list_.clear();
  scenes_.reset();
  if (world_ != GNEISS_NULL_WORLD) {
    (void)gneiss_world_destroy(world_);
    world_ = GNEISS_NULL_WORLD;
  }
  if (platform_initialized_) {
    if (desc_.shutdown != nullptr) {
      desc_.shutdown(desc_.user_data);
    }
    platform_initialized_ = false;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (granit_render_service_ != nullptr) {
    granit::renderer_resource_stats stats;
    shutdown_result = granit_render_service_->shutdown(stats);
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
