// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/log/log.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/input/input_service.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/asset_manager.hpp"
#include "runtime/loading/action_map_application.hpp"
#include "runtime/loading/scene_service.hpp"
#include <atomic>
#include <optional>
#include <thread>
#ifdef GNEISS_NATIVE_LOOP_WAIT
#include "engine/core/loop_progress.hpp"
#endif

#include "engine/resource/vfs/asset_uri.hpp"
#include "engine/resource/vfs/native_file_system.hpp"
#include <cmath>
#ifdef GNEISS_ASSET_LIBUV
#include "engine/platform/io/uv/uv_async_file.hpp"
#include "engine/platform/io/uv/uv_loop_executor.hpp"
#include "engine/platform/io/uv/uv_path_resolver.hpp"
#endif
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/function/world/render_snapshot.hpp"

#ifdef GNEISS_HAS_GRANIT_PLATFORM
#include "engine/function/render/render_service.hpp"
#include "engine/platform/granit/granit_platform.hpp"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <new>
#include <vector>

namespace gneiss::runtime {
struct application::storage {
  explicit storage(const runtime::application_configuration& config)
      : config_(config), owner_thread_(std::this_thread::get_id()) {}
  std::shared_ptr<gneiss::engine::progress_notification> loop_notification_;
  runtime::application_configuration config_;
  gneiss::engine::render_resource_service resources_;
  gneiss::engine::ui_draw_list ui_draw_list_;
  gneiss::engine::debug_draw_list debug_draw_list_;
  std::optional<gneiss::engine::view_camera> view_camera_;
#ifdef GNEISS_ASSET_LIBUV
  std::unique_ptr<gneiss::engine::uv_loop_executor> asset_io_executor_;
  std::shared_ptr<gneiss::engine::native_file_system> native_assets_;
#endif
  gneiss::engine::virtual_file_system asset_file_system_;
  std::unique_ptr<gneiss::engine::asset_manager> asset_manager_;
  std::unique_ptr<gneiss::engine::render_asset_loader> render_assets_;
  runtime::action_map_application input_application_;
  bool asset_executor_attached_ = false;
  runtime::scene_service scene_service_;
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
  gneiss::engine::input_service input_;
  std::unique_ptr<gneiss::engine::log_dispatcher> log_dispatcher_;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  std::unique_ptr<gneiss::engine::granit_platform> granit_platform_;
  std::unique_ptr<gneiss::engine::render_service> render_service_;
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
      timing_ = std::make_unique<gneiss::engine::loop_timing>();
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
  void set_origin(gneiss::engine::timing_clock::time_point origin) noexcept { origin_ = origin; }
  [[nodiscard]] gneiss::engine::loop_timing* get() const noexcept { return timing_.get(); }

private:
  gneiss::engine::timing_clock::time_point origin_;
  std::unique_ptr<std::ofstream> output_;
  std::unique_ptr<gneiss::engine::loop_timing> timing_;
};
} // namespace

gneiss::engine::result application::create(const runtime::application_configuration& config,
                                           std::shared_ptr<application>& output) noexcept {
  if ((config.use_granit_window &&
       (config.window.width == 0U || config.window.height == 0U ||
        config.callbacks.initialize != nullptr || config.callbacks.poll_events != nullptr ||
        config.callbacks.shutdown != nullptr)) ||
      !std::isfinite(config.environment_intensity) || config.environment_intensity < 0.0F ||
      !std::isfinite(config.environment_rotation_radians) ||
      (!config.environment_asset.empty() &&
       gneiss::engine::validate_uri(config.environment_asset) != gneiss::engine::result::success)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto instance = std::shared_ptr<application>(new application(config));
    const auto result = instance->initialize();
    if (result != gneiss::engine::result::success) {
      return result;
    }
    instance->storage_->accepts_requests_.store(true, std::memory_order_release);
    output = std::move(instance);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

application::application(const runtime::application_configuration& config)
    : storage_(std::make_unique<storage>(config)) {}

application::~application() noexcept { static_cast<void>(shutdown()); }

gneiss::engine::render_queue_stats application::render_statistics() const noexcept {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->render_service_) {
    return storage_->render_service_->query_performance_stats();
  }
#endif
  return {};
}

gneiss::engine::result
application::attach_task_executor(gneiss::engine::task_executor& executor) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (storage_->scene_service_.textures()) {
    return gneiss::engine::result::invalid_state;
  }
  try {
#ifdef GNEISS_ASSET_LIBUV
    if (storage_->native_assets_ && !storage_->asset_io_executor_) {
      auto loop = std::make_unique<gneiss::engine::uv_loop_executor>();
      auto files = std::make_shared<gneiss::engine::uv_file_io>(*loop);
      auto paths = std::make_shared<gneiss::engine::uv_path_resolver>(*loop);
      const auto started = loop->start();
      if (started != gneiss::engine::result::success) {
        return started;
      }
      storage_->native_assets_->set_async_io(std::move(files), std::move(paths));
      storage_->asset_io_executor_ = std::move(loop);
    }
#endif
    storage_->asset_manager_->attach_task_executor(executor);
    storage_->asset_executor_attached_ = true;
    gneiss::engine::texture_upload_backend backend;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    if (storage_->render_service_) {
      backend.begin = [this](auto data, auto& sequence) {
        return storage_->render_service_->prepare_textures(std::move(data), sequence);
      };
      backend.poll = [this](auto sequence, auto& result) {
        return storage_->render_service_->poll_texture_preparation(sequence, result);
      };
      backend.discard = [this](auto data, auto& sequence) {
        return storage_->render_service_->discard_prepared_textures(std::move(data), sequence);
      };
      backend.profile = storage_->render_service_->texture_profile();
      backend.estimate_bytes = gneiss::engine::render_service::estimate_upload_bytes;
      backend.elapsed_ms = [this] { return storage_->render_service_->latest_texture_upload_ms(); };
      backend.flush = [this] { (void)storage_->render_service_->finish_frames(); };
    } else
#endif
    {
      // 无渲染平台的 application 只验证 CPU 与资源事务，不宣称 GPU 上传成功。
      backend.begin = [](auto, auto& sequence) {
        sequence = 1U;
        return gneiss::engine::result::success;
      };
      backend.poll = [](auto, auto& result) {
        result = gneiss::engine::result::success;
        return true;
      };
      backend.discard = backend.begin;
      backend.flush = [] {};
    }
    return storage_->scene_service_.attach_executor(
        executor, storage_->resources_, std::move(backend), storage_->loop_notification_);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result application::activate_scene(std::uint64_t request,
                                                   runtime::scene_load_completion& completion) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  try {
    return storage_->scene_service_.activate(request, completion);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result application::reload_render_assets(
    std::span<const gneiss::engine::render_asset_reload> assets) noexcept {
  if (scene_loading()) {
    return gneiss::engine::result::not_ready;
  }
  gneiss::engine::asset_diagnostic diagnostic;
  return storage_->render_assets_->reload_assets(assets, diagnostic);
}

gneiss::engine::result application::reload_scene(gneiss::scene_instance_id instance,
                                                 std::string_view uri) noexcept {
  if (scene_loading()) {
    return gneiss::engine::result::not_ready;
  }
  return scenes() == nullptr ? gneiss::engine::result::invalid_state
                             : scenes()->reload(instance, uri);
}

gneiss::engine::result application::reload_prefab(gneiss::scene_instance_id instance,
                                                  std::string_view uri) noexcept {
  if (scene_loading()) {
    return gneiss::engine::result::not_ready;
  }
  return scenes() == nullptr ? gneiss::engine::result::invalid_state
                             : scenes()->reload_prefab(instance, uri);
}

gneiss::engine::result application::initialize() noexcept {
  if (storage_->config_.callbacks.log != nullptr) {
    try {
      storage_->log_dispatcher_ =
          std::make_unique<gneiss::engine::log_dispatcher>(storage_->config_.callbacks.log);
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }
  if (!storage_->resources_.is_valid()) {
    return gneiss::engine::result::out_of_memory;
  }
  if (!storage_->config_.asset_root.empty()) {
    std::shared_ptr<gneiss::engine::native_file_system> native_file_system;
    try {
      native_file_system = std::make_shared<gneiss::engine::native_file_system>();
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
    auto mount_result = native_file_system->initialize(storage_->config_.asset_root);
    if (mount_result == gneiss::engine::result::success) {
#ifdef GNEISS_ASSET_LIBUV
      storage_->native_assets_ = native_file_system;
#endif
      mount_result = storage_->asset_file_system_.mount("asset://", std::move(native_file_system));
    }
    if (mount_result != gneiss::engine::result::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::asset, mount_result,
             "asset", "资产根目录挂载失败");
      return mount_result;
    }
  }
  if (storage_->config_.use_granit_window) {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    try {
      storage_->granit_platform_ = std::make_unique<gneiss::engine::granit_platform>();
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
    const auto platform_result = storage_->granit_platform_->initialize(storage_->config_.window);
    if (platform_result != gneiss::engine::result::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::backend, platform_result,
             "granit.platform", "Granit 平台初始化失败");
      storage_->granit_platform_.reset();
      return platform_result;
    }
    try {
      storage_->render_service_ = std::make_unique<gneiss::engine::render_service>();
#ifdef GNEISS_NATIVE_LOOP_WAIT
      storage_->loop_notification_ = std::make_shared<gneiss::engine::loop_progress>();
#endif
    } catch (const std::bad_alloc&) {
      storage_->granit_platform_.reset();
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      storage_->granit_platform_.reset();
      return gneiss::engine::result::internal;
    }
    std::vector<std::byte> environment_asset;
    if (!storage_->config_.environment_asset.empty()) {
      const auto environment_result =
          storage_->asset_file_system_.read(storage_->config_.environment_asset, environment_asset);
      if (environment_result != gneiss::engine::result::success) {
        report(gneiss::engine::severity::error, gneiss::engine::category::asset, environment_result,
               "environment", "环境资产读取失败");
        storage_->granit_platform_.reset();
        return environment_result;
      }
    }
    const auto render_result = storage_->render_service_->initialize(
        storage_->granit_platform_->native_window(), environment_asset,
        storage_->config_.environment_intensity, storage_->config_.environment_rotation_radians,
        storage_->log_dispatcher_.get());
    if (render_result != gneiss::engine::result::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::backend, render_result,
             "granit.render", "Granit 渲染服务初始化失败");
      storage_->render_service_.reset();
      storage_->granit_platform_.reset();
      return render_result;
    }
    storage_->render_service_->set_completion_notification(storage_->loop_notification_);
#else
    report(gneiss::engine::severity::error, gneiss::engine::category::backend,
           gneiss::engine::result::unsupported, "granit.platform",
           "当前构建未启用 Granit 平台适配");
    return gneiss::engine::result::unsupported;
#endif
  }
  if (storage_->config_.callbacks.initialize != nullptr) {
    auto result = gneiss::engine::result::success;
    try {
      result = storage_->config_.callbacks.initialize();
    } catch (const std::bad_alloc&) {
      result = gneiss::engine::result::out_of_memory;
    } catch (...) {
      result = gneiss::engine::result::internal;
    }
    if (result != gneiss::engine::result::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::application, result,
             "application.initialize", "application 初始化回调失败");
      // 初始化回调可能已获得部分资源，因此失败时也执行配对清理。
      storage_->platform_initialized_ = true;
      static_cast<void>(shutdown());
      return result;
    }
  }
  storage_->platform_initialized_ = true;

  try {
    storage_->asset_manager_ =
        std::make_unique<gneiss::engine::asset_manager>(storage_->asset_file_system_);
    storage_->render_assets_ = std::make_unique<gneiss::engine::render_asset_loader>(
        *storage_->asset_manager_, storage_->resources_);
    const auto result =
        storage_->scene_service_.initialize(*storage_->asset_manager_, *storage_->render_assets_);
    if (result != gneiss::engine::result::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::application, result,
             "scene", "场景域创建失败");
      static_cast<void>(shutdown());
      return result;
    }
  } catch (const std::bad_alloc&) {
    static_cast<void>(shutdown());
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    static_cast<void>(shutdown());
    return gneiss::engine::result::internal;
  }
  return gneiss::engine::result::success;
}

std::uint64_t application::now_ns() const {
  if (storage_->config_.callbacks.now_ns != nullptr) {
    return storage_->config_.callbacks.now_ns();
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

gneiss::engine::result application::poll_events(bool& out_should_close) noexcept {
  GNEISS_PROFILE_SCOPE("application.events");
  out_should_close = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_platform_ != nullptr) {
    storage_->input_.begin_frame();
    bool focus_lost = false;
    const auto platform_result = storage_->granit_platform_->poll(out_should_close, focus_lost);
    if (platform_result != gneiss::engine::result::success) {
      return platform_result;
    }
    gneiss::engine::keyboard_state keyboard = {};
    auto input_result =
        gneiss::engine::measure(gneiss::engine::loop_stage::input_state, [&]() noexcept {
          return storage_->granit_platform_->keyboard(keyboard);
        });
    if (input_result != gneiss::engine::result::success) {
      return input_result;
    }
    gneiss::engine::pointer_state pointer = {};
    input_result = gneiss::engine::measure(gneiss::engine::loop_stage::input_state, [&]() noexcept {
      return storage_->granit_platform_->pointer(pointer);
    });
    if (input_result != gneiss::engine::result::success) {
      return input_result;
    }
    storage_->input_.set_keyboard(keyboard);
    storage_->input_.set_pointer(pointer);
    gneiss::engine::input_event event = {};
    input_result =
        gneiss::engine::measure(gneiss::engine::loop_stage::input_events, [&]() noexcept {
          return storage_->granit_platform_->poll_input(event);
        });
    while (input_result == gneiss::engine::result::success) {
      if (!storage_->input_.push(event)) {
        storage_->input_.clear_focus();
        return gneiss::engine::result::invalid_state;
      }
      event = {};
      input_result =
          gneiss::engine::measure(gneiss::engine::loop_stage::input_events, [&]() noexcept {
            return storage_->granit_platform_->poll_input(event);
          });
    }
    if (focus_lost) {
      storage_->input_.clear_focus();
    }
    return input_result == gneiss::engine::result::not_ready ? gneiss::engine::result::success
                                                             : input_result;
  }
#endif
  if (storage_->config_.callbacks.poll_events == nullptr) {
    return gneiss::engine::result::success;
  }
  try {
    return storage_->config_.callbacks.poll_events(out_should_close);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result application::poll_input(gneiss::engine::input_event& out_event) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  return storage_->input_.poll(out_event);
}

gneiss::engine::result application::begin_action_map(std::string_view uri) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (!storage_->asset_executor_attached_)
    return gneiss::engine::result::not_ready;
  storage_->input_application_.cancel();
  gneiss::engine::asset_request<gneiss::engine::action_map> request;
  const auto started = storage_->asset_manager_->request_input(uri, request);
  return started == gneiss::engine::result::success
             ? storage_->input_application_.begin(std::move(request))
             : started;
}
gneiss::engine::result application::action_map_status() const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  return storage_->asset_executor_attached_ ? storage_->input_application_.status()
                                            : gneiss::engine::result::not_ready;
}
gneiss::engine::result application::cancel_action_map() noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (!storage_->asset_executor_attached_)
    return gneiss::engine::result::not_ready;
  storage_->input_application_.cancel();
  return gneiss::engine::result::success;
}
gneiss::engine::result application::load_action_map(std::string_view uri) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  storage_->input_application_.cancel();
  gneiss::engine::action_map map;
  const auto result = storage_->asset_manager_->load_input(uri, map);
  return result == gneiss::engine::result::success
             ? storage_->input_.replace_action_map(std::move(map))
             : result;
}

gneiss::engine::result
application::find_action(std::string_view name,
                         gneiss::engine::action_id& out_action) const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  return storage_->input_.find_action(name, out_action);
}

gneiss::engine::result
application::get_action_state(gneiss::engine::action_id action,
                              gneiss::engine::action_state& out_state) const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  return storage_->input_.get_action_state(action, out_state);
}

gneiss::engine::result
application::submit_ui_draw_list(const gneiss::engine::ui_draw_view& desc) noexcept {
  return storage_->is_updating_ ? storage_->ui_draw_list_.replace(desc, storage_->resources_)
                                : gneiss::engine::result::invalid_state;
}

gneiss::engine::result
application::submit_debug_draw_list(std::span<const gneiss::engine::debug_line> lines) noexcept {
  return storage_->is_updating_ ? storage_->debug_draw_list_.replace(lines)
                                : gneiss::engine::result::invalid_state;
}

void application::report(gneiss::engine::severity severity, gneiss::engine::category category,
                         gneiss::engine::result result, std::string_view module,
                         std::string_view message) noexcept {
  if (storage_->config_.callbacks.diagnostic != nullptr) {
    const gneiss::engine::diagnostic_view diagnostic{.level = severity,
                                                     .source = category,
                                                     .result = gneiss::engine::result{result},
                                                     .module = module,
                                                     .message = message};
    try {
      storage_->config_.callbacks.diagnostic(diagnostic);
    } catch (...) {
      // 诊断接收方不能中断初始化、关闭或错误日志投递。
    }
  }
  const auto log_severity = severity == gneiss::engine::severity::info ? gneiss::engine::level::info
                            : severity == gneiss::engine::severity::warning
                                ? gneiss::engine::level::warning
                                : gneiss::engine::level::error;
  const std::string_view log_category = category == gneiss::engine::category::asset   ? "asset"
                                        : category == gneiss::engine::category::input ? "input"
                                        : category == gneiss::engine::category::backend
                                            ? "backend"
                                            : "application";
  static_cast<void>(submit_log({
      .severity = log_severity,
      .source = module,
      .category = log_category,
      .message = message,
      .result = gneiss::engine::result{result},
  }));
}

gneiss::engine::result
application::submit_log(const gneiss::engine::message_view& message) noexcept {
  return storage_->log_dispatcher_ == nullptr ? gneiss::engine::validate_message(message)
                                              : storage_->log_dispatcher_->submit(message);
}

gneiss::engine::result
application::set_view_camera(const gneiss::engine::view_camera* camera) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (camera == nullptr) {
    storage_->view_camera_.reset();
    return gneiss::engine::result::success;
  }
  gneiss::engine::matrix4 matrix;
  auto result = gneiss::engine::build_view_matrix(camera->transform, matrix);
  if (result == gneiss::engine::result::success) {
    result = gneiss::engine::build_perspective_matrix(camera->camera, 1.0F, matrix);
  }
  if (result == gneiss::engine::result::success) {
    storage_->view_camera_ = *camera;
  }
  return result;
}

gneiss::engine::result application::capture_frame(std::uint32_t width, std::uint32_t height,
                                                  gneiss::engine::frame_image& output) noexcept {
  output = {};
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (width == 0U || height == 0U || width > 1024U || height > 1024U) {
    return gneiss::engine::result::invalid_argument;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (!storage_->render_service_) {
    return gneiss::engine::result::unsupported;
  }
  try {
    auto window = storage_->granit_platform_->native_window();
    window.width = width;
    window.height = height;
    window.needs_recreate = false;
    gneiss::engine::render_snapshot snapshot;
    auto result = storage_->scene_service_.native_world()->get_render_snapshot(
        width, height, snapshot, storage_->view_camera_ ? &*storage_->view_camera_ : nullptr);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    gneiss::engine::render_frame_packet packet;
    result = gneiss::engine::capture_render_frame_packet(
        window, std::move(snapshot), storage_->resources_, storage_->ui_draw_list_,
        storage_->debug_draw_list_, packet);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    auto image = std::make_shared<gneiss::engine::frame_image>();
    packet.readback = image;
    std::uint64_t sequence{};
    result = storage_->render_service_->submit(
        std::move(packet), gneiss::engine::render_frame_policy::required, &sequence);
    if (result != gneiss::engine::result::success) {
      return result;
    }
    result = storage_->render_service_->finish_frames();
    if (result != gneiss::engine::result::success) {
      return result;
    }
    gneiss::engine::render_frame_completion completed;
    if (!storage_->render_service_->try_take_required_completion(completed) ||
        completed.sequence != sequence || completed.status != gneiss::engine::result::success) {
      return gneiss::engine::result::internal;
    }
    output = std::move(*image);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
#else
  return gneiss::engine::result::unsupported;
#endif
}

#ifdef GNEISS_HAS_GRANIT_PLATFORM
gneiss::engine::result application::render_frame() noexcept {
  GNEISS_PROFILE_SCOPE("application.render.submit");
  if (storage_->render_service_ == nullptr) {
    return gneiss::engine::result::success;
  }
  auto& window = storage_->granit_platform_->native_window();
  if (window.width == 0U || window.height == 0U) {
    return gneiss::engine::result::success;
  }
  gneiss::engine::render_frame_packet packet;
  bool should_prepare = false;
  const auto storage_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::frame_storage, [&]() noexcept {
        return storage_->render_service_->prepare_frame_packet_storage(packet, should_prepare);
      });
  if (storage_result != gneiss::engine::result::success || !should_prepare) {
    return storage_result;
  }
  gneiss::engine::render_snapshot snapshot;
  const auto snapshot_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::render_snapshot, [&]() noexcept {
        return storage_->scene_service_.native_world()->get_render_snapshot(
            window.width, window.height, snapshot,
            storage_->view_camera_ ? &*storage_->view_camera_ : nullptr);
      });
  if (snapshot_result != gneiss::engine::result::success) {
    return snapshot_result;
  }
  const auto capture_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::frame_capture, [&]() noexcept {
        return gneiss::engine::capture_render_frame_packet(
            window, std::move(snapshot), storage_->resources_, storage_->ui_draw_list_,
            storage_->debug_draw_list_, packet);
      });
  if (capture_result != gneiss::engine::result::success) {
    return capture_result;
  }
  const auto requested_recreate = window.needs_recreate;
  window.needs_recreate = false;
  const auto submit_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::frame_submit, [&]() noexcept {
        return storage_->render_service_->submit(std::move(packet));
      });
  if (submit_result != gneiss::engine::result::success && requested_recreate) {
    window.needs_recreate = true;
  }
  // 成功推进队列后先继续排空本地可做工作，队列繁忙且无进展时才等待。
  if (submit_result == gneiss::engine::result::success && storage_->loop_notification_) {
    storage_->loop_notification_->notify();
  }
  return submit_result;
}
#else
gneiss::engine::result application::render_frame() noexcept {
  return gneiss::engine::result::unsupported;
}
#endif

gneiss::engine::result application::get_window_size(std::uint32_t& out_width,
                                                    std::uint32_t& out_height) const noexcept {
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->granit_platform_ != nullptr) {
    const auto& window = storage_->granit_platform_->native_window();
    out_width = window.width;
    out_height = window.height;
    return gneiss::engine::result::success;
  }
#endif
  out_width = storage_->config_.window.width;
  out_height = storage_->config_.window.height;
  return gneiss::engine::result::success;
}

gneiss::engine::result application::run(std::uint64_t max_frame_count) noexcept try {
  if (!is_owner_thread() || !storage_->platform_initialized_ || storage_->is_running_) {
    return gneiss::engine::result::invalid_state;
  }
  storage_->is_running_ = true;
  storage_->should_exit_ = false;
  loop_trace_file trace;
  const auto trace_origin = trace.get() != nullptr ? gneiss::engine::timing_clock::now()
                                                   : gneiss::engine::timing_clock::time_point{};
  trace.set_origin(trace_origin);
  storage_->previous_time_ns_ = now_ns();
  std::uint64_t frames_run = 0;
  GNEISS_PROFILE_THREAD("Gneiss main");

  while (!storage_->should_exit_ && (max_frame_count == 0U || frames_run < max_frame_count)) {
    GNEISS_PROFILE_FRAME();
    GNEISS_PROFILE_SCOPE("application.loop");
    const gneiss::engine::loop_frame frame_trace(trace.get(), storage_->frame_index_, trace_origin);
#ifdef GNEISS_NATIVE_LOOP_WAIT
    auto* progress =
        static_cast<gneiss::engine::loop_progress*>(storage_->loop_notification_.get());
    const auto observed = progress != nullptr ? progress->snapshot() : 0U;
#endif
    bool should_close = false;
    const auto poll_result = gneiss::engine::measure(gneiss::engine::loop_stage::events,
                                                     [&] { return poll_events(should_close); });
    if (poll_result != gneiss::engine::result::success) {
      storage_->is_running_ = false;
      return poll_result;
    }
    if (should_close) {
      if (storage_->config_.callbacks.close_requested == nullptr ||
          storage_->config_.callbacks.close_requested()) {
        break;
      }
    }

    storage_->asset_manager_->advance_resources();
    (void)storage_->input_application_.poll(storage_->input_);
    const auto current_time_ns = now_ns();
    const auto raw_delta = current_time_ns >= storage_->previous_time_ns_
                               ? current_time_ns - storage_->previous_time_ns_
                               : 0U;
    storage_->previous_time_ns_ = current_time_ns;
    const auto delta_ns = storage_->is_paused_ ? 0U : raw_delta;
    storage_->elapsed_ns_ += delta_ns;
    const gneiss::engine::frame_time time = {
        .frame_index = storage_->frame_index_,
        .delta_ns = delta_ns,
        .elapsed_ns = storage_->elapsed_ns_,
        .is_paused = storage_->is_paused_,
    };
    gneiss::engine::measure(gneiss::engine::loop_stage::draw_clear, [&] {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
    });
    if (storage_->config_.callbacks.update != nullptr) {
      storage_->is_updating_ = true;
      const auto update_result = gneiss::engine::measure(gneiss::engine::loop_stage::update, [&] {
        GNEISS_PROFILE_SCOPE("application.update");
        return storage_->config_.callbacks.update(time);
      });
      storage_->is_updating_ = false;
      if (update_result != gneiss::engine::result::success) {
        storage_->ui_draw_list_.clear();
        storage_->debug_draw_list_.clear();
        storage_->is_running_ = false;
        return update_result;
      }
    }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
    const auto render_result =
        gneiss::engine::measure(gneiss::engine::loop_stage::render, [&] { return render_frame(); });
    // 渲染队列、交换链或后端资源可能暂时未就绪；跳过本帧并在下一帧重试。
    if (render_result != gneiss::engine::result::success &&
        render_result != gneiss::engine::result::not_ready) {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
      storage_->is_running_ = false;
      return render_result;
    }
#endif
    gneiss::engine::measure(gneiss::engine::loop_stage::draw_clear, [&] {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
    });
    ++storage_->frame_index_;
    ++frames_run;
#ifdef GNEISS_NATIVE_LOOP_WAIT
    if (progress != nullptr && !storage_->should_exit_ &&
        (max_frame_count == 0U || frames_run < max_frame_count)) {
      gneiss::engine::measure(gneiss::engine::loop_stage::idle_wait, [&] {
        GNEISS_PROFILE_SCOPE("application.idle_wait");
        (void)progress->wait(observed, std::chrono::milliseconds(4));
      });
    }
#endif
  }

  storage_->is_running_ = false;
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  // 返回前回收已提交帧，避免短程运行把尚未回传的上传失败误报为成功。
  if (storage_->render_service_ != nullptr) {
    return storage_->render_service_->finish_frames();
  }
#endif
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  storage_->is_running_ = false;
  storage_->is_updating_ = false;
  storage_->ui_draw_list_.clear();
  storage_->debug_draw_list_.clear();
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  storage_->is_running_ = false;
  storage_->is_updating_ = false;
  storage_->ui_draw_list_.clear();
  storage_->debug_draw_list_.clear();
  return gneiss::engine::result::internal;
}

bool application::is_owner_thread() const noexcept {
  return storage_->owner_thread_ == std::this_thread::get_id();
}

gneiss::engine::result application::shutdown() noexcept {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_) {
    return gneiss::engine::result::invalid_state;
  }
  storage_->shutting_down_ = true;
  storage_->accepts_requests_.store(false, std::memory_order_release);
  auto shutdown_result = gneiss::engine::result::success;
  storage_->input_application_.cancel();
  if (storage_->asset_manager_) {
    storage_->asset_manager_->stop_resources();
  }
  storage_->scene_service_.shutdown();
  storage_->render_assets_.reset();
  storage_->asset_manager_.reset();
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
        shutdown_result = gneiss::engine::result::out_of_memory;
      } catch (...) {
        shutdown_result = gneiss::engine::result::internal;
      }
    }
    storage_->platform_initialized_ = false;
  }
#ifdef GNEISS_HAS_GRANIT_PLATFORM
  if (storage_->render_service_ != nullptr) {
    const auto render_stats = storage_->render_service_->query_performance_stats();
    gneiss::engine::render_device_stats stats;
    const auto render_shutdown_result = storage_->render_service_->shutdown(stats);
    if (shutdown_result == gneiss::engine::result::success) {
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
    report(gneiss::engine::severity::info, gneiss::engine::category::backend,
           gneiss::engine::result::success, "granit.render.performance",
           std::string_view(performance_message.data(), performance_length));
    if (shutdown_result != gneiss::engine::result::success) {
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
      report(gneiss::engine::severity::error, gneiss::engine::category::backend, shutdown_result,
             "granit.render.resources", std::string_view(message.data(), length));
    }
  }
  storage_->render_service_.reset();
  storage_->granit_platform_.reset();
#else
#endif
#ifdef GNEISS_ASSET_LIBUV
  // 渲染镜像/已提交帧仍可能持有纹理容器的文件源。
  // 必须先释放渲染端最后的快照，再关闭文件执行器；否则 join 等待仍被引用的 uv handle。
  storage_->asset_io_executor_.reset();
#endif
  (void)storage_->resources_.drain_retired_resources();
  storage_->shutting_down_ = false;
  return shutdown_result;
}

const gneiss::engine::virtual_file_system& application::asset_files() const noexcept {
  return storage_->asset_file_system_;
}

bool application::has_asset_loading_service() const noexcept {
  return storage_->scene_service_.textures() != nullptr;
}

gneiss::engine::texture_load_service* application::texture_service() noexcept {
  return storage_->scene_service_.textures();
}

gneiss::engine::result application::create_mesh_node_ready(
    gneiss::scene_instance_id scene, const gneiss::engine::mesh_renderer_node_creation& desc,
    const gneiss::engine::scene_instances::mesh_binding& prepared, scene_node_id& output) noexcept {
  output = {};
  const auto valid = is_owner_thread() && accepts_requests()
                         ? gneiss::engine::result::success
                         : gneiss::engine::result::invalid_state;
  return valid == gneiss::engine::result::success
             ? scenes()->create_mesh_renderer_node_ready(scene, desc, prepared, output)
             : valid;
}

gneiss::engine::result application::collect_unused_edit_assets() noexcept {
  const auto valid = is_owner_thread() && accepts_requests()
                         ? gneiss::engine::result::success
                         : gneiss::engine::result::invalid_state;
  if (valid == gneiss::engine::result::success) {
    storage_->asset_manager_->collect_unused();
  }
  return valid;
}

gneiss::engine::result
application::query_ready_asset(const gneiss::engine::render_asset_reload& source,
                               gneiss::engine::render_asset_ref& output) noexcept {
  output = {};
  const auto valid = is_owner_thread() && accepts_requests()
                         ? gneiss::engine::result::success
                         : gneiss::engine::result::invalid_state;
  if (valid != gneiss::engine::result::success) {
    return valid;
  }
  auto* service = texture_service();
  const auto result =
      service ? storage_->render_assets_->acquire_cached(source, output, service->profile())
              : gneiss::engine::result::not_ready;
  return result == gneiss::engine::result::not_found ? gneiss::engine::result::not_ready : result;
}

gneiss::engine::result
application::capture_mesh_binding(gneiss::scene_instance_id scene, scene_node_id node,
                                  gneiss::engine::scene_instances::mesh_binding& output) noexcept {
  output = {};
  const auto valid = is_owner_thread() && accepts_requests()
                         ? gneiss::engine::result::success
                         : gneiss::engine::result::invalid_state;
  return valid == gneiss::engine::result::success
             ? scenes()->capture_mesh_binding(scene, node, output)
             : valid;
}

gneiss::engine::result application::replace_mesh_binding(
    gneiss::scene_instance_id scene, scene_node_id node,
    const gneiss::engine::scene_instances::mesh_binding& expected,
    const gneiss::engine::scene_instances::mesh_binding& replacement) noexcept {
  const auto valid = is_owner_thread() && accepts_requests()
                         ? gneiss::engine::result::success
                         : gneiss::engine::result::invalid_state;
  return valid == gneiss::engine::result::success
             ? scenes()->replace_mesh_binding(scene, node, expected, replacement)
             : valid;
}

gneiss::engine::result
application::prepare_edit_assets(std::span<const gneiss::engine::render_asset_reload> sources,
                                 std::uint64_t revision, std::uint64_t& request) noexcept try {
  request = 0U;
  const auto valid = is_owner_thread() && accepts_requests()
                         ? gneiss::engine::result::success
                         : gneiss::engine::result::invalid_state;
  if (valid != gneiss::engine::result::success) {
    return valid;
  }
  auto* service = texture_service();
  if (!service || scene_loading() || visual_assets_loading() || service->busy()) {
    return gneiss::engine::result::not_ready;
  }
  return service->submit_assets(sources, 1U, revision, request, true,
                                gneiss::engine::texture_load_service::maximum_prepared_bytes);
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result application::request_scene(std::string_view uri, std::uint64_t session,
                                                  std::uint64_t revision, std::uint64_t& request) {
  request = 0;
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  try {
    return storage_->scene_service_.request_scene(uri, session, revision, request);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

bool application::scene_progress(runtime::scene_load_progress& progress) const {
  progress = {};
  if (!is_owner_thread() || !accepts_requests())
    return false;
  return storage_->scene_service_.scene_progress(progress);
}

gneiss::engine::result application::poll_scene(runtime::scene_load_completion& completion,
                                               bool& ready) {
  ready = false;
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  try {
    const auto result = storage_->scene_service_.poll_scene(completion, ready);
    report_visual_completion();
    return result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result application::cancel_scene(std::uint64_t request) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  try {
    return storage_->scene_service_.cancel_scene(request);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

bool application::scene_loading() const noexcept { return storage_->scene_service_.loading(); }

gneiss::engine::result
application::request_assets(std::span<const gneiss::engine::render_asset_reload> sources,
                            std::uint64_t session, std::uint64_t revision, std::uint64_t& request,
                            bool reload) {
  request = 0;
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  if (!texture_service() || scene_loading())
    return gneiss::engine::result::not_ready;
  try {
    return storage_->scene_service_.request_assets(sources, session, revision, request, reload);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result application::poll_assets(gneiss::engine::texture_load_completion& completion,
                                                bool& ready) {
  ready = false;
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  try {
    return storage_->scene_service_.poll_assets(completion, ready);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

bool application::asset_progress(gneiss::engine::asset_load_progress& progress) const noexcept {
  progress = {};
  if (!is_owner_thread() || !accepts_requests())
    return false;
  auto* service = storage_->scene_service_.textures();
  return service && service->progress(progress);
}

gneiss::engine::result application::cancel_assets() noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::result::invalid_state;
  auto* service = texture_service();
  return service && service->cancel() ? gneiss::engine::result::success
                                      : gneiss::engine::result::not_ready;
}

void application::advance_scene_load() {
  (void)storage_->resources_.drain_retired_resources();
  storage_->scene_service_.advance();
  report_visual_completion();
}

void application::report_visual_completion() {
  gneiss::engine::texture_load_completion completion;
  if (storage_->scene_service_.take_visual_completion(completion) &&
      completion.state != gneiss::engine::texture_load_state::cancelled) {
    for (const auto& item : completion.results) {
      if (!item.applied) {
        report(gneiss::engine::severity::error, gneiss::engine::category::asset, item.result,
               "scene.assets", item.source.uri);
      }
    }
  }
}

bool application::can_activate_scene() const noexcept {
  return storage_->scene_service_.can_activate();
}

bool application::visual_assets_loading() const noexcept {
  return storage_->scene_service_.visual_loading();
}

runtime::scene_retirement_statistics application::scene_retirement() const noexcept {
  auto value = storage_->scene_service_.retirement();
  value.live_resources = storage_->resources_.live_resource_count();
  return value;
}

bool application::accepts_requests() const noexcept {
  return storage_->accepts_requests_.load(std::memory_order_acquire);
}

gneiss::engine::world* application::native_world() const noexcept {
  return storage_->scene_service_.native_world();
}

gneiss::engine::render_resource_service& application::resources() noexcept {
  return storage_->resources_;
}

gneiss::engine::asset_manager& application::assets() noexcept { return *storage_->asset_manager_; }

gneiss::engine::scene_instances* application::scenes() noexcept {
  return storage_->scene_service_.scenes();
}

const gneiss::engine::keyboard_state& application::keyboard_state() const noexcept {
  return storage_->input_.keyboard();
}

const gneiss::engine::pointer_state& application::pointer_state() const noexcept {
  return storage_->input_.pointer();
}

bool application::can_submit_draw_lists() const noexcept { return storage_->is_updating_; }

void application::request_exit() noexcept { storage_->should_exit_ = true; }

void application::set_paused(bool value) noexcept { storage_->is_paused_ = value; }

} // namespace gneiss::runtime
