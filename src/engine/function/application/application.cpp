// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#ifdef GNEISS_NATIVE_LOOP_WAIT
#include "engine/core/loop_progress.hpp"
#endif

#include "engine/asset/asset_uri.hpp"
#include "engine/asset/native_file_system.hpp"
#include <cmath>
#ifdef GNEISS_ASSET_LIBUV
#include "engine/platform/io/uv/uv_async_file.hpp"
#include "engine/platform/io/uv/uv_loop_executor.hpp"
#include "engine/platform/io/uv/uv_path_resolver.hpp"
#endif
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/function/world/render_snapshot.hpp"

#ifdef GNEISS_HAS_GRANIT_PLATFORM
#include "engine/function/render/backend/granit/granit_render_service.hpp"
#include "engine/platform/granit/granit_platform.hpp"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <new>
#include <vector>

namespace gneiss::engine {
struct application::storage {
  explicit storage(const application_internal::application_configuration& config)
      : config_(config), owner_thread_(std::this_thread::get_id()) {}
  std::shared_ptr<core::progress_notification> loop_notification_;
  application_internal::application_configuration config_;
  render_internal::render_resource_service resources_;
  render_internal::ui_draw_list ui_draw_list_;
  render_internal::debug_draw_list debug_draw_list_;
  std::optional<render_internal::view_camera> view_camera_;
#ifdef GNEISS_ASSET_LIBUV
  std::unique_ptr<io_internal::uv_loop_executor> asset_io_executor_;
  std::shared_ptr<asset_internal::native_file_system> native_assets_;
#endif
  asset_internal::virtual_file_system asset_file_system_;
  std::unique_ptr<application_internal::application_asset_state> asset_state_;
  application_internal::scene_session scene_session_;
  std::thread::id owner_thread_;
  std::uint64_t frame_index_ = 0;
  std::uint64_t elapsed_ns_ = 0;
  std::uint64_t previous_time_ns_ = 0;
  std::atomic<bool> accepts_requests_{false};
  bool shutting_down_ = false;
  bool platform_initialized_ = false;
  bool is_running_ = false;
  bool is_paused_ = false;
  bool should_exit_ = false;
  bool is_updating_ = false;
  input_internal::input_service input_;
  std::unique_ptr<log_internal::log_dispatcher> log_dispatcher_;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  std::unique_ptr<platform::granit_platform> granit_platform_;
  std::unique_ptr<render_internal::granit_render_service> granit_render_service_;
#endif
};

namespace {
class loop_trace_file final {
public:
  loop_trace_file() noexcept {
    try {
#ifdef _WIN32
      char* value = nullptr;
      std::size_t length{};
      if (_dupenv_s(&value, &length, "GNEISS_LOOP_TRACE") != 0) {
        return;
      }
      const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
#else
      const auto* value = std::getenv("GNEISS_LOOP_TRACE");
#endif
      if (value == nullptr || *value == '\0') {
        return;
      }
      output_ = std::make_unique<std::ofstream>(value, std::ios::app);
      if (!*output_) {
        std::fputs("Gneiss 循环诊断：无法打开输出\n", stderr);
        return;
      }
      timing_ = std::make_unique<diagnostics::loop_timing>();
    } catch (...) {
      std::fputs("Gneiss 循环诊断：初始化失败\n", stderr);
    }
  }
  ~loop_trace_file() {
    if (timing_) {
      try {
        *output_ << "# origin_ns="
                 << std::chrono::duration_cast<std::chrono::nanoseconds>(origin_.time_since_epoch())
                        .count()
                 << '\n'
                 << std::setprecision(10);
        timing_->write(*output_);
        output_->flush();
        if (!*output_) {
          std::fputs("Gneiss 循环诊断：写入失败\n", stderr);
        }
      } catch (...) {
        std::fputs("Gneiss 循环诊断：写入失败\n", stderr);
      }
    }
  }
  void set_origin(diagnostics::timing_clock::time_point origin) noexcept { origin_ = origin; }
  [[nodiscard]] diagnostics::loop_timing* get() const noexcept { return timing_.get(); }

private:
  diagnostics::timing_clock::time_point origin_;
  std::unique_ptr<std::ofstream> output_;
  std::unique_ptr<diagnostics::loop_timing> timing_;
};
} // namespace

gneiss_result application::create(const application_internal::application_configuration& config,
                                  std::shared_ptr<application>& output) noexcept {
  if ((config.use_granit_window &&
       (config.window.width == 0U || config.window.height == 0U ||
        config.callbacks.initialize != nullptr || config.callbacks.poll_events != nullptr ||
        config.callbacks.shutdown != nullptr)) ||
      !std::isfinite(config.environment_intensity) || config.environment_intensity < 0.0F ||
      !std::isfinite(config.environment_rotation_radians) ||
      (!config.environment_asset.empty() &&
       asset_internal::validate_uri(config.environment_asset) != GNEISS_SUCCESS)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto instance = std::shared_ptr<application>(new application(config));
    const auto result = instance->initialize();
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    instance->storage_->accepts_requests_.store(true, std::memory_order_release);
    output = std::move(instance);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

application::application(const application_internal::application_configuration& config)
    : storage_(std::make_unique<storage>(config)) {}

application::~application() noexcept { static_cast<void>(shutdown(GNEISS_NULL_APPLICATION)); }

render_internal::render_queue_stats application::render_statistics() const noexcept {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_render_service_) {
    return storage_->granit_render_service_->query_performance_stats();
  }
#endif
  return {};
}

gneiss_result application::attach_task_executor(tasks::task_executor& executor) noexcept {
  if (storage_->scene_session_.textures()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  try {
#ifdef GNEISS_ASSET_LIBUV
    if (storage_->native_assets_ && !storage_->asset_io_executor_ &&
        executor.mode() == tasks::execution_mode::thread_pool) {
      auto loop = std::make_unique<io_internal::uv_loop_executor>();
      auto files = std::make_shared<io_internal::uv_file_io>(*loop);
      auto paths = std::make_shared<io_internal::uv_path_resolver>(*loop);
      const auto started = loop->start();
      if (started != result::success) {
        return started.native();
      }
      storage_->native_assets_->set_async_io(std::move(files), std::move(paths));
      storage_->asset_io_executor_ = std::move(loop);
    }
#endif
    render_internal::texture_upload_backend backend;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    if (storage_->granit_render_service_) {
      backend.begin = [this](auto data, auto& sequence) {
        return storage_->granit_render_service_->prepare_textures(std::move(data), sequence);
      };
      backend.poll = [this](auto sequence, auto& result) {
        return storage_->granit_render_service_->poll_texture_preparation(sequence, result);
      };
      backend.discard = [this](auto data, auto& sequence) {
        return storage_->granit_render_service_->discard_prepared_textures(std::move(data),
                                                                           sequence);
      };
      backend.profile = storage_->granit_render_service_->texture_profile();
      backend.estimate_bytes = render_internal::granit_render_service::estimate_upload_bytes;
      backend.elapsed_ms = [this] {
        return storage_->granit_render_service_->latest_texture_upload_ms();
      };
      backend.flush = [this] { (void)storage_->granit_render_service_->finish_frames(); };
    } else
#endif
    {
      // 无渲染平台的 application 只验证 CPU 与资源事务，不宣称 GPU 上传成功。
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
    return storage_->scene_session_.attach_executor(executor, storage_->asset_file_system_,
                                                    storage_->resources_, std::move(backend),
                                                    storage_->loop_notification_);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result application::activate_scene(std::uint64_t request,
                                          application_internal::scene_load_completion& completion) {
  return storage_->scene_session_.activate(request, completion);
}

gneiss_result application::reload_render_assets(
    std::span<const render_internal::render_asset_reload> assets) noexcept {
  if (scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  render_internal::asset_diagnostic diagnostic;
  return storage_->scene_session_.assets().reload_assets(assets, diagnostic);
}

gneiss_result application::reload_scene(gneiss_scene_instance instance,
                                        std::string_view uri) noexcept {
  if (scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return scenes() == nullptr ? GNEISS_ERROR_INVALID_STATE : scenes()->reload(instance, uri);
}

gneiss_result application::reload_prefab(gneiss_scene_instance instance,
                                         std::string_view uri) noexcept {
  if (scene_loading()) {
    return GNEISS_ERROR_NOT_READY;
  }
  return scenes() == nullptr ? GNEISS_ERROR_INVALID_STATE : scenes()->reload_prefab(instance, uri);
}

gneiss_result application::initialize() noexcept {
  if (storage_->config_.callbacks.log != nullptr) {
    try {
      storage_->log_dispatcher_ = std::make_unique<log_internal::log_dispatcher>(
          storage_->config_.callbacks.log);
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }
  if (!storage_->resources_.is_valid()) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
  if (!storage_->config_.asset_root.empty()) {
    std::shared_ptr<asset_internal::native_file_system> native_file_system;
    try {
      native_file_system = std::make_shared<asset_internal::native_file_system>();
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
    auto mount_result = native_file_system->initialize(storage_->config_.asset_root);
    if (mount_result == GNEISS_SUCCESS) {
#ifdef GNEISS_ASSET_LIBUV
      storage_->native_assets_ = native_file_system;
#endif
      mount_result = storage_->asset_file_system_.mount("asset://", std::move(native_file_system));
    }
    if (mount_result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_ASSET,
             mount_result, "asset", "资产根目录挂载失败");
      return mount_result;
    }
  }
  if (storage_->config_.use_granit_window) {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    try {
      storage_->granit_platform_ = std::make_unique<platform::granit_platform>();
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
    const auto platform_result = storage_->granit_platform_->initialize(storage_->config_.window);
    if (platform_result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
             platform_result, "granit.platform", "Granit 平台初始化失败");
      storage_->granit_platform_.reset();
      return platform_result;
    }
    try {
      storage_->granit_render_service_ = std::make_unique<render_internal::granit_render_service>();
#ifdef GNEISS_NATIVE_LOOP_WAIT
      storage_->loop_notification_ = std::make_shared<core::loop_progress>();
#endif
    } catch (const std::bad_alloc&) {
      storage_->granit_platform_.reset();
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      storage_->granit_platform_.reset();
      return GNEISS_ERROR_INTERNAL;
    }
    std::vector<std::byte> environment_asset;
    if (!storage_->config_.environment_asset.empty()) {
      const auto environment_result =
          storage_->asset_file_system_.read(storage_->config_.environment_asset, environment_asset);
      if (environment_result != GNEISS_SUCCESS) {
        report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_ASSET,
               environment_result, "environment", "环境资产读取失败");
        storage_->granit_platform_.reset();
        return environment_result;
      }
    }
    const auto render_result = storage_->granit_render_service_->initialize(
        storage_->granit_platform_->native_window(), environment_asset,
        storage_->config_.environment_intensity, storage_->config_.environment_rotation_radians,
        storage_->log_dispatcher_.get());
    if (render_result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
             render_result, "granit.render", "Granit 渲染服务初始化失败");
      storage_->granit_render_service_.reset();
      storage_->granit_platform_.reset();
      return render_result;
    }
    storage_->granit_render_service_->set_completion_notification(storage_->loop_notification_);
#else
    report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR, GNEISS_DIAGNOSTIC_CATEGORY_BACKEND,
           GNEISS_ERROR_UNSUPPORTED, "granit.platform", "当前构建未启用 Granit 平台适配");
    return GNEISS_ERROR_UNSUPPORTED;
#endif
  }
  if (storage_->config_.callbacks.initialize != nullptr) {
    auto result = GNEISS_SUCCESS;
    try {
      result = to_native(storage_->config_.callbacks.initialize());
    } catch (const std::bad_alloc&) {
      result = GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      result = GNEISS_ERROR_INTERNAL;
    }
    if (result != GNEISS_SUCCESS) {
      report(GNEISS_NULL_APPLICATION, GNEISS_DIAGNOSTIC_ERROR,
             GNEISS_DIAGNOSTIC_CATEGORY_APPLICATION, result, "application.initialize",
             "application 初始化回调失败");
      // 初始化回调可能已获得部分资源，因此失败时也执行配对清理。
      storage_->platform_initialized_ = true;
      static_cast<void>(shutdown(GNEISS_NULL_APPLICATION));
      return result;
    }
  }
  storage_->platform_initialized_ = true;

  try {
    storage_->asset_state_ = std::make_unique<application_internal::application_asset_state>(
        storage_->asset_file_system_, storage_->resources_);
    const auto result = storage_->scene_session_.initialize(*storage_->asset_state_);
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

std::uint64_t application::now_ns() const noexcept {
  if (storage_->config_.callbacks.now_ns != nullptr) {
    return storage_->config_.callbacks.now_ns(storage_->config_.callbacks.user_data);
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

gneiss_result application::poll_events(bool& out_should_close) noexcept {
  GNEISS_PROFILE_SCOPE("application.events");
  out_should_close = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_platform_ != nullptr) {
    storage_->input_.begin_frame();
    bool focus_lost = false;
    const auto platform_result = storage_->granit_platform_->poll(out_should_close, focus_lost);
    if (platform_result != GNEISS_SUCCESS) {
      return platform_result;
    }
    gneiss_keyboard_state keyboard = GNEISS_KEYBOARD_STATE_INIT;
    auto input_result = diagnostics::measure(diagnostics::loop_stage::input_state, [&]() noexcept {
      return storage_->granit_platform_->keyboard(keyboard);
    });
    if (input_result != GNEISS_SUCCESS) {
      return input_result;
    }
    gneiss_pointer_state pointer = GNEISS_POINTER_STATE_INIT;
    input_result = diagnostics::measure(diagnostics::loop_stage::input_state, [&]() noexcept {
      return storage_->granit_platform_->pointer(pointer);
    });
    if (input_result != GNEISS_SUCCESS) {
      return input_result;
    }
    storage_->input_.set_keyboard(keyboard);
    storage_->input_.set_pointer(pointer);
    gneiss_input_event event = GNEISS_INPUT_EVENT_INIT;
    input_result = diagnostics::measure(diagnostics::loop_stage::input_events, [&]() noexcept {
      return storage_->granit_platform_->poll_input(event);
    });
    while (input_result == GNEISS_SUCCESS) {
      if (!storage_->input_.push(event)) {
        storage_->input_.clear_focus();
        return GNEISS_ERROR_INVALID_STATE;
      }
      event = GNEISS_INPUT_EVENT_INIT;
      input_result = diagnostics::measure(diagnostics::loop_stage::input_events, [&]() noexcept {
        return storage_->granit_platform_->poll_input(event);
      });
    }
    if (focus_lost) {
      storage_->input_.clear_focus();
    }
    return input_result == GNEISS_ERROR_NOT_READY ? GNEISS_SUCCESS : input_result;
  }
#endif
  if (storage_->config_.callbacks.poll_events == nullptr) {
    return GNEISS_SUCCESS;
  }
  try {
    return to_native(storage_->config_.callbacks.poll_events(out_should_close));
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result application::poll_input(gneiss_input_event& out_event) noexcept {
  return storage_->input_.poll(out_event);
}

gneiss_result application::load_action_map(std::string_view uri) noexcept {
  input_internal::action_map map;
  const auto result = input_internal::load_action_map(storage_->asset_file_system_, uri, map);
  return result == GNEISS_SUCCESS ? storage_->input_.replace_action_map(std::move(map)) : result;
}

gneiss_result application::find_action(std::string_view name,
                                       gneiss_action& out_action) const noexcept {
  return storage_->input_.find_action(name, out_action);
}

gneiss_result application::get_action_state(gneiss_action action,
                                            gneiss_action_state& out_state) const noexcept {
  return storage_->input_.get_action_state(action, out_state);
}

gneiss_result application::submit_ui_draw_list(const render_internal::ui_draw_view& desc) noexcept {
  return storage_->is_updating_ ? storage_->ui_draw_list_.replace(desc, storage_->resources_)
                                : GNEISS_ERROR_INVALID_STATE;
}

gneiss_result
application::submit_debug_draw_list(std::span<const gneiss_debug_line> lines) noexcept {
  return storage_->is_updating_ ? storage_->debug_draw_list_.replace(lines)
                                : GNEISS_ERROR_INVALID_STATE;
}

void application::report(gneiss_application handle, std::uint32_t severity, std::uint32_t category,
                         gneiss_result result, std::string_view module,
                         std::string_view message) noexcept {
  if (storage_->config_.callbacks.diagnostic != nullptr) {
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
    storage_->config_.callbacks.diagnostic(handle, &diagnostic,
                                           storage_->config_.callbacks.user_data);
  }
  const auto log_severity = severity == GNEISS_DIAGNOSTIC_INFO      ? log_internal::level::info
                            : severity == GNEISS_DIAGNOSTIC_WARNING ? log_internal::level::warning
                                                                    : log_internal::level::error;
  const std::string_view log_category = category == GNEISS_DIAGNOSTIC_CATEGORY_ASSET   ? "asset"
                                        : category == GNEISS_DIAGNOSTIC_CATEGORY_INPUT ? "input"
                                        : category == GNEISS_DIAGNOSTIC_CATEGORY_BACKEND
                                            ? "backend"
                                            : "application";
  static_cast<void>(submit_log({
      .context = handle,
      .severity = log_severity,
      .source = module,
      .category = log_category,
      .message = message,
      .result = from_native(result),
  }));
}

gneiss_result application::submit_log(const log_internal::message_view& message) noexcept {
  return storage_->log_dispatcher_ == nullptr ? GNEISS_SUCCESS
                                              : to_native(storage_->log_dispatcher_->submit(message));
}

gneiss_result application::set_view_camera(const render_internal::view_camera* camera) noexcept {
  if (camera == nullptr) {
    storage_->view_camera_.reset();
    return GNEISS_SUCCESS;
  }
  render_internal::matrix4 matrix;
  auto result = render_internal::build_view_matrix(camera->transform, matrix);
  if (result == GNEISS_SUCCESS) {
    result = render_internal::build_perspective_matrix(camera->camera, 1.0F, matrix);
  }
  if (result == GNEISS_SUCCESS) {
    storage_->view_camera_ = *camera;
  }
  return result;
}

gneiss_result application::capture_frame(std::uint32_t width, std::uint32_t height,
                                         render_internal::frame_image& output) noexcept {
  output = {};
  if (width == 0U || height == 0U || width > 1024U || height > 1024U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (!storage_->granit_render_service_) {
    return GNEISS_ERROR_UNSUPPORTED;
  }
  try {
    auto window = storage_->granit_platform_->native_window();
    window.width = width;
    window.height = height;
    window.needs_recreate = false;
    render_internal::render_snapshot snapshot;
    auto result = world_internal::get_render_snapshot(
        world(), width, height, snapshot,
        storage_->view_camera_ ? &*storage_->view_camera_ : nullptr);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    render_internal::render_frame_packet packet;
    result = render_internal::capture_render_frame_packet(
        window, std::move(snapshot), storage_->resources_, storage_->ui_draw_list_,
        storage_->debug_draw_list_, packet);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    auto image = std::make_shared<render_internal::frame_image>();
    packet.readback = image;
    std::uint64_t sequence{};
    result = storage_->granit_render_service_->submit(
        std::move(packet), render_internal::render_frame_policy::required, &sequence);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    result = storage_->granit_render_service_->finish_frames();
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    render_internal::render_frame_completion completed;
    if (!storage_->granit_render_service_->try_take_required_completion(completed) ||
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
gneiss_result application::render_frame() noexcept {
  GNEISS_PROFILE_SCOPE("application.render.submit");
  if (storage_->granit_render_service_ == nullptr) {
    return GNEISS_SUCCESS;
  }
  auto& window = storage_->granit_platform_->native_window();
  if (window.width == 0U || window.height == 0U) {
    return GNEISS_SUCCESS;
  }
  render_internal::render_frame_packet packet;
  bool should_prepare = false;
  const auto storage_result =
      diagnostics::measure(diagnostics::loop_stage::frame_storage, [&]() noexcept {
        return storage_->granit_render_service_->prepare_frame_packet_storage(packet,
                                                                              should_prepare);
      });
  if (storage_result != GNEISS_SUCCESS || !should_prepare) {
    return storage_result;
  }
  render_internal::render_snapshot snapshot;
  const auto snapshot_result =
      diagnostics::measure(diagnostics::loop_stage::render_snapshot, [&]() noexcept {
        return world_internal::get_render_snapshot(world(), window.width, window.height, snapshot,
                                                   storage_->view_camera_ ? &*storage_->view_camera_
                                                                          : nullptr);
      });
  if (snapshot_result != GNEISS_SUCCESS) {
    return snapshot_result;
  }
  const auto capture_result =
      diagnostics::measure(diagnostics::loop_stage::frame_capture, [&]() noexcept {
        return render_internal::capture_render_frame_packet(
            window, std::move(snapshot), storage_->resources_, storage_->ui_draw_list_,
            storage_->debug_draw_list_, packet);
      });
  if (capture_result != GNEISS_SUCCESS) {
    return capture_result;
  }
  const auto requested_recreate = window.needs_recreate;
  window.needs_recreate = false;
  const auto submit_result =
      diagnostics::measure(diagnostics::loop_stage::frame_submit, [&]() noexcept {
        return storage_->granit_render_service_->submit(std::move(packet));
      });
  if (submit_result != GNEISS_SUCCESS && requested_recreate) {
    window.needs_recreate = true;
  }
  // 成功推进队列后先继续排空本地可做工作，队列繁忙且无进展时才等待。
  if (submit_result == GNEISS_SUCCESS && storage_->loop_notification_) {
    storage_->loop_notification_->notify();
  }
  return submit_result;
}
#else
gneiss_result application::render_frame() noexcept { return GNEISS_ERROR_UNSUPPORTED; }
#endif

gneiss_result application::get_window_size(std::uint32_t& out_width,
                                           std::uint32_t& out_height) const noexcept {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_platform_ != nullptr) {
    const auto& window = storage_->granit_platform_->native_window();
    out_width = window.width;
    out_height = window.height;
    return GNEISS_SUCCESS;
  }
#endif
  out_width = storage_->config_.window.width;
  out_height = storage_->config_.window.height;
  return GNEISS_SUCCESS;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 句柄与帧数虽同宽但语义明确。
gneiss_result application::run(gneiss_application handle, std::uint64_t max_frame_count) noexcept {
  if (!is_owner_thread() || !storage_->platform_initialized_ || storage_->is_running_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  storage_->is_running_ = true;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_render_service_ != nullptr) {
    storage_->granit_render_service_->set_log_application(handle);
  }
#endif
  storage_->should_exit_ = false;
  loop_trace_file trace;
  const auto trace_origin = trace.get() != nullptr ? diagnostics::timing_clock::now()
                                                   : diagnostics::timing_clock::time_point{};
  trace.set_origin(trace_origin);
  storage_->previous_time_ns_ = now_ns();
  std::uint64_t frames_run = 0;
  GNEISS_PROFILE_THREAD("Gneiss main");

  while (!storage_->should_exit_ && (max_frame_count == 0U || frames_run < max_frame_count)) {
    GNEISS_PROFILE_FRAME();
    GNEISS_PROFILE_SCOPE("application.loop");
    const diagnostics::loop_frame frame_trace(trace.get(), storage_->frame_index_, trace_origin);
#ifdef GNEISS_NATIVE_LOOP_WAIT
    auto* progress = static_cast<core::loop_progress*>(storage_->loop_notification_.get());
    const auto observed = progress != nullptr ? progress->snapshot() : 0U;
#endif
    bool should_close = false;
    const auto poll_result = diagnostics::measure(diagnostics::loop_stage::events,
                                                  [&] { return poll_events(should_close); });
    if (poll_result != GNEISS_SUCCESS) {
      storage_->is_running_ = false;
      return poll_result;
    }
    if (should_close) {
      if (storage_->config_.callbacks.close_requested == nullptr ||
          storage_->config_.callbacks.close_requested(
              handle, storage_->config_.callbacks.user_data) != 0U) {
        break;
      }
    }

    const auto current_time_ns = now_ns();
    const auto raw_delta = current_time_ns >= storage_->previous_time_ns_
                               ? current_time_ns - storage_->previous_time_ns_
                               : 0U;
    storage_->previous_time_ns_ = current_time_ns;
    const auto delta_ns = storage_->is_paused_ ? 0U : raw_delta;
    storage_->elapsed_ns_ += delta_ns;
    const gneiss_frame_time time = {
        .frame_index = storage_->frame_index_,
        .delta_ns = delta_ns,
        .elapsed_ns = storage_->elapsed_ns_,
        .is_paused = static_cast<std::uint8_t>(storage_->is_paused_ ? 1U : 0U),
        .reserved = {},
    };
    diagnostics::measure(diagnostics::loop_stage::draw_clear, [&] {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
    });
    if (storage_->config_.callbacks.update != nullptr) {
      storage_->is_updating_ = true;
      const auto update_result = diagnostics::measure(diagnostics::loop_stage::update, [&] {
        GNEISS_PROFILE_SCOPE("application.update");
        return storage_->config_.callbacks.update(handle, &time,
                                                  storage_->config_.callbacks.user_data);
      });
      storage_->is_updating_ = false;
      if (update_result != GNEISS_SUCCESS) {
        storage_->ui_draw_list_.clear();
        storage_->debug_draw_list_.clear();
        storage_->is_running_ = false;
        return update_result;
      }
    }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    const auto render_result =
        diagnostics::measure(diagnostics::loop_stage::render, [&] { return render_frame(); });
    // 渲染队列、交换链或后端资源可能暂时未就绪；跳过本帧并在下一帧重试。
    if (render_result != GNEISS_SUCCESS && render_result != GNEISS_ERROR_NOT_READY) {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
      storage_->is_running_ = false;
      return render_result;
    }
#endif
    diagnostics::measure(diagnostics::loop_stage::draw_clear, [&] {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
    });
    ++storage_->frame_index_;
    ++frames_run;
#ifdef GNEISS_NATIVE_LOOP_WAIT
    if (progress != nullptr && !storage_->should_exit_ &&
        (max_frame_count == 0U || frames_run < max_frame_count)) {
      diagnostics::measure(diagnostics::loop_stage::idle_wait, [&] {
        GNEISS_PROFILE_SCOPE("application.idle_wait");
        (void)progress->wait(observed, std::chrono::milliseconds(4));
      });
    }
#endif
  }

  storage_->is_running_ = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  // 返回前回收已提交帧，避免短程运行把尚未回传的上传失败误报为成功。
  if (storage_->granit_render_service_ != nullptr) {
    return storage_->granit_render_service_->finish_frames();
  }
#endif
  return GNEISS_SUCCESS;
}

bool application::is_owner_thread() const noexcept {
  return storage_->owner_thread_ == std::this_thread::get_id();
}

gneiss_result application::shutdown(gneiss_application handle) noexcept {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  storage_->shutting_down_ = true;
  storage_->accepts_requests_.store(false, std::memory_order_release);
  auto shutdown_result = GNEISS_SUCCESS;
  storage_->scene_session_.shutdown();
  storage_->asset_state_.reset();
  storage_->is_updating_ = false;
  storage_->ui_draw_list_.clear();
  storage_->debug_draw_list_.clear();
#ifdef GNEISS_ASSET_LIBUV
  if (storage_->native_assets_) {
    storage_->native_assets_->set_async_io({}, {});
  }
#endif
  if (storage_->platform_initialized_) {
    if (storage_->config_.callbacks.shutdown != nullptr) {
      try {
        storage_->config_.callbacks.shutdown();
      } catch (const std::bad_alloc&) {
        shutdown_result = GNEISS_ERROR_OUT_OF_MEMORY;
      } catch (...) {
        shutdown_result = GNEISS_ERROR_INTERNAL;
      }
    }
    storage_->platform_initialized_ = false;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_render_service_ != nullptr) {
    const auto render_stats = storage_->granit_render_service_->query_performance_stats();
    granit::renderer_resource_stats stats;
    const auto render_shutdown_result = storage_->granit_render_service_->shutdown(stats);
    if (shutdown_result == GNEISS_SUCCESS) {
      shutdown_result = render_shutdown_result;
    }
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
  storage_->granit_render_service_.reset();
  storage_->granit_platform_.reset();
#else
  (void)handle;
#endif
#ifdef GNEISS_ASSET_LIBUV
  // 渲染镜像/已提交帧仍可能持有纹理容器的文件源。
  // 必须先释放渲染端最后的快照，再关闭文件执行器；否则 join 等待仍被引用的 uv handle。
  storage_->asset_io_executor_.reset();
#endif
  storage_->shutting_down_ = false;
  return shutdown_result;
}

const asset_internal::virtual_file_system& application::asset_files() const noexcept {
  return storage_->asset_file_system_;
}

render_internal::texture_load_service* application::texture_service() noexcept {
  return storage_->scene_session_.textures();
}

gneiss_result
application::accept_edit_assets(const render_internal::texture_load_completion& completion,
                                const std::function<gneiss_result()>& accept) {
  return storage_->scene_session_.accept_edit_assets(completion, accept);
}

gneiss_result application::request_scene(std::string_view uri, std::uint64_t session,
                                         std::uint64_t revision, std::uint64_t& request) {
  return storage_->scene_session_.request_scene(uri, session, revision, request);
}

bool application::scene_progress(application_internal::scene_load_progress& progress) const {
  return storage_->scene_session_.scene_progress(progress);
}

gneiss_result application::poll_scene(application_internal::scene_load_completion& completion,
                                      bool& ready) {
  return storage_->scene_session_.poll_scene(completion, ready);
}

gneiss_result application::cancel_scene(std::uint64_t request) {
  return storage_->scene_session_.cancel_scene(request);
}

bool application::scene_loading() const noexcept { return storage_->scene_session_.loading(); }

gneiss_result
application::request_assets(std::span<const render_internal::render_asset_reload> sources,
                            std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                            bool reload) {
  return storage_->scene_session_.request_assets(sources, session, revision, request, reload);
}

gneiss_result application::poll_assets(render_internal::texture_load_completion& completion,
                                       bool& ready) {
  return storage_->scene_session_.poll_assets(completion, ready);
}

void application::advance_scene_load() { storage_->scene_session_.advance(); }

application_internal::scene_retirement_statistics application::scene_retirement() const noexcept {
  auto value = storage_->scene_session_.retirement();
  value.live_resources = storage_->resources_.live_resource_count();
  return value;
}

bool application::accepts_requests() const noexcept {
  return storage_->accepts_requests_.load(std::memory_order_acquire);
}

gneiss_world application::world() const noexcept { return storage_->scene_session_.world(); }

render_internal::render_resource_service& application::resources() noexcept {
  return storage_->resources_;
}

render_internal::render_asset_loader& application::asset_loader() noexcept {
  return storage_->scene_session_.assets();
}

scene_internal::prefab_asset_loader& application::prefab_loader() noexcept {
  return storage_->asset_state_->prefabs;
}

scene_internal::scene_instance_service* application::scenes() noexcept {
  return storage_->scene_session_.scenes();
}

const gneiss_keyboard_state& application::keyboard_state() const noexcept {
  return storage_->input_.keyboard();
}

const gneiss_pointer_state& application::pointer_state() const noexcept {
  return storage_->input_.pointer();
}

bool application::can_submit_draw_lists() const noexcept { return storage_->is_updating_; }

void application::request_exit() noexcept { storage_->should_exit_ = true; }

void application::set_paused(bool value) noexcept { storage_->is_paused_ = value; }

} // namespace gneiss::engine
