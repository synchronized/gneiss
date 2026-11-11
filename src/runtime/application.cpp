// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/diagnostics/profiling.hpp"
#include "engine/core/error.hpp"
#include "engine/core/log/log.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/detail/resource_load_group.hpp"
#include "engine/function/input/input_service.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/render_extraction.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/platform/files/file_service.hpp"
#include "runtime/loading/scene_service.hpp"
#include <atomic>
#include <optional>
#include <thread>
#include <tuple>
#ifdef GNEISS_NATIVE_LOOP_WAIT
#include "runtime/main_loop_wakeup.hpp"
#endif

#include "engine/resource/asset_uri.hpp"
#include <cmath>
#ifdef __EMSCRIPTEN__
#include "engine/platform/files/browser/browser_file_backend.hpp"
#include "engine/platform/files/browser/fetch_file_backend.hpp"
#endif
#ifdef GNEISS_ASSET_LIBUV
#include "engine/platform/files/native_file_backend.hpp"
#include "engine/platform/io/uv/uv_loop_executor.hpp"

#endif

#include "engine/platform/window_service.hpp"

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
  std::shared_ptr<gneiss::engine::wakeup_signal> loop_wakeup_;
  runtime::application_configuration config_;
  std::unique_ptr<gneiss::engine::render_service> render_service_ =
      std::make_unique<gneiss::engine::render_service>();
  gneiss::engine::ui_draw_list ui_draw_list_;
  gneiss::engine::debug_draw_list debug_draw_list_;
  std::optional<gneiss::engine::view_camera> view_camera_;
#ifdef GNEISS_ASSET_LIBUV
  std::unique_ptr<gneiss::engine::uv_loop_executor> asset_io_executor_;
#endif
  std::shared_ptr<gneiss::engine::files::service> files_;
  struct environment_read {
    bool ready{};
    engine::error status{engine::error::not_ready};
    std::shared_ptr<const std::vector<std::byte>> bytes;
  };
  std::shared_ptr<environment_read> environment_;
  std::unique_ptr<gneiss::engine::asset_manager> asset_manager_;
  engine::asset_request<engine::action_map> pending_action_map_;
  std::shared_ptr<std::atomic<bool>> action_map_ready_;
  engine::error action_map_result_{engine::error::invalid_state};

  void cancel_action_map() noexcept {
    action_map_ready_.reset();
    pending_action_map_.cancel();
    action_map_result_ = engine::error::invalid_state;
  }
  engine::world_owner world_;
  runtime::scene_service scene_service_;
  std::unique_ptr<engine::resource_load_group> asset_loading_;
  std::thread::id owner_thread_;
  std::uint64_t frame_index_ = 0;
  std::uint64_t elapsed_ns_ = 0;
  std::uint64_t previous_time_ns_ = 0;
  std::atomic<bool> accepts_requests_{false};
  bool shutting_down_ = false;
  bool shutdown_requested_{};
  bool shutdown_complete_{};
  gneiss::engine::error shutdown_result_{gneiss::engine::error::success};
  struct file_close_receipt {
    bool ready{};
    gneiss::engine::error result{gneiss::engine::error::success};
  };
  std::shared_ptr<file_close_receipt> file_close_;
  std::shared_ptr<gneiss::engine::files::service> closing_files_;
  bool platform_initialized_ = false;
  bool is_running_ = false;
  bool is_paused_ = false;
  bool should_exit_ = false;
  bool is_updating_ = false;
  gneiss::engine::input_service input_;
  std::unique_ptr<gneiss::engine::log_dispatcher> log_dispatcher_;
  std::unique_ptr<gneiss::engine::window_service> window_service_;
};

namespace {
class loop_trace_file final {
public:
  loop_trace_file() {
    {
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
    }
  }
  ~loop_trace_file() {
    if (timing_) {
      {
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

engine::result<std::shared_ptr<application>>
application::create(const application_configuration& config) {
  if ((config.enable_window &&
       (config.window.width == 0U || config.window.height == 0U ||
        config.callbacks.initialize != nullptr || config.callbacks.poll_events != nullptr ||
        config.callbacks.shutdown != nullptr)) ||
      !std::isfinite(config.environment_intensity) || config.environment_intensity < 0.0F ||
      !std::isfinite(config.environment_rotation_radians) ||
      (!config.environment_asset.empty() &&
       gneiss::engine::validate_uri(config.environment_asset) != gneiss::engine::error::success)) {
    return gneiss::engine::error::invalid_argument;
  }
  {
    const auto meta_status = gneiss::engine::initialize_meta();
    if (meta_status.failed())
      return meta_status;
    auto instance = std::shared_ptr<application>(new application(config));
    const auto result = instance->initialize();
    if (result != gneiss::engine::error::success) {
      return result;
    }
    instance->storage_->accepts_requests_.store(true, std::memory_order_release);
    return instance;
  }
}

application::application(const runtime::application_configuration& config)
    : storage_(std::make_unique<storage>(config)) {}

application::~application() noexcept { static_cast<void>(shutdown()); }

gneiss::engine::render_queue_stats application::render_statistics() const {
  if (storage_->window_service_) {
    return storage_->render_service_->query_performance_stats();
  }
  return {};
}

gneiss::engine::error application::attach_task_executor(gneiss::engine::task_executor& executor) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (storage_->asset_loading_.get()) {
    return gneiss::engine::error::invalid_state;
  }
  {
    gneiss::engine::texture_prepare_profile profile;
    if (storage_->window_service_)
      profile = storage_->render_service_->texture_profile();
    const auto scope = executor.make_scope(storage_->loop_wakeup_);
    if (!scope.id)
      return engine::error::invalid_state;
    const auto configured = storage_->asset_manager_->configure(executor, profile, scope);
    if (configured.failed()) {
      (void)executor.close_scope(scope);
      return configured;
    }
    auto group = std::make_unique<engine::resource_load_group>(*storage_->asset_manager_);
    const auto attached = storage_->scene_service_.configure(*storage_->render_service_);
    if (attached.failed())
      return attached;
    storage_->asset_loading_ = std::move(group);
    return engine::error::success;
  }
}

gneiss::engine::error application::initialize() {
  if (storage_->config_.callbacks.log != nullptr) {
    {
      storage_->log_dispatcher_ =
          std::make_unique<gneiss::engine::log_dispatcher>(storage_->config_.callbacks.log);
    }
  }
  if (!storage_->render_service_->is_valid()) {
    return gneiss::engine::error::out_of_memory;
  }
  {
    storage_->files_ = std::make_shared<gneiss::engine::files::service>(
        gneiss::engine::files::service_options{.max_file_bytes = 64U * 1024U * 1024U});
    for (const auto& [prefix, root, writable] :
         {std::tuple<std::string_view, std::string_view, bool>{
              "asset://", storage_->config_.asset_root, storage_->config_.writable_assets},
          {"user://", storage_->config_.user_root, true}}) {
      if (root.empty())
        continue;
      std::shared_ptr<gneiss::engine::files::backend> backend;
#ifdef GNEISS_ASSET_LIBUV
      if (!storage_->asset_io_executor_) {
        storage_->asset_io_executor_ = std::make_unique<engine::uv_loop_executor>();
        const auto started = storage_->asset_io_executor_->start();
        if (started.failed())
          return started;
      }
      backend = std::make_shared<gneiss::engine::files::native_backend>(
          *storage_->asset_io_executor_, std::string(root));
#elif defined(__EMSCRIPTEN__)
      auto base = std::string(root);
      if (base.starts_with('/') && !base.starts_with("//")) {
        while (base.size() > 1 && base.ends_with('/'))
          base.pop_back();
        auto local = std::make_shared<gneiss::engine::files::browser_backend>(
            std::move(base), gneiss::engine::files::browser_storage::existing);
        const auto initialized = local->initialize([](engine::error) {});
        if (initialized.failed())
          return initialized;
        backend = std::move(local);
      } else {
        if (!base.ends_with('/'))
          base += '/';
        backend = std::make_shared<gneiss::engine::files::fetch_backend>(std::move(base));
      }
#else
      return engine::error::unsupported;
#endif
      const auto mounted = storage_->files_->mount({.prefix = std::string(prefix),
                                                    .readers = {backend},
                                                    .writer = writable ? backend : nullptr,
                                                    .source = {}});
      if (mounted.failed())
        return mounted;
    }
  }
  if (storage_->config_.enable_window) {
    {
      storage_->window_service_ = std::make_unique<gneiss::engine::window_service>();
    }
    const auto platform_result = storage_->window_service_->initialize(storage_->config_.window);
    if (platform_result != gneiss::engine::error::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::backend, platform_result,
             "granit.platform", "Granit 平台初始化失败");
      storage_->window_service_.reset();
      return platform_result;
    }
    try {
#ifdef GNEISS_NATIVE_LOOP_WAIT
      storage_->loop_wakeup_ = std::make_shared<gneiss::runtime::main_loop_wakeup>();
#endif
    } catch (...) {
      storage_->window_service_.reset();
      throw;
    }
    const auto render_result = storage_->render_service_->initialize(
        storage_->window_service_->native_window(), {}, storage_->config_.environment_intensity,
        storage_->config_.environment_rotation_radians, storage_->log_dispatcher_.get());
    if (render_result != gneiss::engine::error::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::backend, render_result,
             "granit.render", "Granit 渲染服务初始化失败");
      storage_->window_service_.reset();
      return render_result;
    }
    storage_->render_service_->set_completion_wakeup(storage_->loop_wakeup_);
  }
  if (storage_->config_.callbacks.initialize != nullptr) {
    auto result = gneiss::engine::error::success;
    {
      result = storage_->config_.callbacks.initialize();
    }
    if (result != gneiss::engine::error::success) {
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
    storage_->asset_manager_ = std::make_unique<gneiss::engine::asset_manager>(storage_->files_);
    if (storage_->asset_manager_->attach_render(*storage_->render_service_).failed())
      return engine::error::invalid_state;
    auto created_world = engine::create_native();
    if (!created_world)
      return created_world.error();
    storage_->world_ = std::move(*created_world);
    const auto result =
        storage_->scene_service_.initialize(*storage_->asset_manager_, *storage_->world_);
    if (result != gneiss::engine::error::success) {
      report(gneiss::engine::severity::error, gneiss::engine::category::application, result,
             "scene", "场景域创建失败");
      static_cast<void>(shutdown());
      return result;
    }
  } catch (...) {
    static_cast<void>(shutdown());
    throw;
  }
  // 完成所有宿主/场景初始化后才提交 I/O，失败回退不遗留后台文件请求。
  if (storage_->window_service_ && !storage_->config_.environment_asset.empty()) {
    {
      auto state = std::make_shared<storage::environment_read>();
      gneiss::engine::files::request_id request;
      const auto accepted = storage_->files_->read(
          storage_->config_.environment_asset, {.max_bytes = 64U * 1024U * 1024U},
          [state](gneiss::engine::files::completion<gneiss::engine::files::bytes> value) {
            state->status = value.status;
            if (value.status.ok()) {
              {
                state->bytes =
                    std::make_shared<const std::vector<std::byte>>(std::move(value.data));
              }
            }
            state->ready = true;
          },
          request);
      if (accepted.failed())
        return accepted;
      storage_->environment_ = std::move(state);
    }
  }
  return gneiss::engine::error::success;
}

std::uint64_t application::now_ns() const {
  if (storage_->config_.callbacks.now_ns != nullptr) {
    return storage_->config_.callbacks.now_ns();
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

gneiss::engine::error application::poll_events(bool& out_should_close) {
  GNEISS_PROFILE_SCOPE("application.events");
  out_should_close = false;
  if (storage_->window_service_ != nullptr) {
    storage_->input_.begin_frame();
    bool focus_lost = false;
    const auto platform_result = storage_->window_service_->poll(out_should_close, focus_lost);
    if (platform_result != gneiss::engine::error::success) {
      return platform_result;
    }
    gneiss::engine::keyboard_state keyboard = {};
    auto input_result = gneiss::engine::measure(gneiss::engine::loop_stage::input_state, [&]() {
      return storage_->window_service_->keyboard(keyboard);
    });
    if (input_result != gneiss::engine::error::success) {
      return input_result;
    }
    gneiss::engine::pointer_state pointer = {};
    input_result = gneiss::engine::measure(gneiss::engine::loop_stage::input_state, [&]() {
      return storage_->window_service_->pointer(pointer);
    });
    if (input_result != gneiss::engine::error::success) {
      return input_result;
    }
    storage_->input_.set_keyboard(keyboard);
    storage_->input_.set_pointer(pointer);
    gneiss::engine::input_event event = {};
    input_result = gneiss::engine::measure(gneiss::engine::loop_stage::input_events, [&]() {
      return storage_->window_service_->poll_input(event);
    });
    while (input_result == gneiss::engine::error::success) {
      if (!storage_->input_.push(event)) {
        storage_->input_.clear_focus();
        return gneiss::engine::error::invalid_state;
      }
      event = {};
      input_result = gneiss::engine::measure(gneiss::engine::loop_stage::input_events, [&]() {
        return storage_->window_service_->poll_input(event);
      });
    }
    if (focus_lost) {
      storage_->input_.clear_focus();
    }
    return input_result == gneiss::engine::error::not_ready ? gneiss::engine::error::success
                                                            : input_result;
  }
  if (storage_->config_.callbacks.poll_events == nullptr) {
    return gneiss::engine::error::success;
  }
  {
    return storage_->config_.callbacks.poll_events(out_should_close);
  }
}

gneiss::engine::error application::poll_input(gneiss::engine::input_event& out_event) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  return storage_->input_.poll(out_event);
}

gneiss::engine::error application::begin_action_map(std::string_view uri) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (!has_asset_loading_service())
    return gneiss::engine::error::not_ready;
  storage_->cancel_action_map();
  auto requested = storage_->asset_manager_->reload<gneiss::engine::action_map>(uri);
  if (!requested)
    return requested.error();
  storage_->pending_action_map_ = std::move(*requested);
  storage_->action_map_result_ = engine::error::not_ready;
  storage_->action_map_ready_ = std::make_shared<std::atomic<bool>>(false);
  // 每个请求独立的完成通知；取消或替换后，旧通知不能影响当前请求。
  engine::detail::asset_request_access::when_ready(
      storage_->pending_action_map_, [ready = std::weak_ptr{storage_->action_map_ready_}] {
        if (auto receipt = ready.lock())
          receipt->store(true);
      });
  return engine::error::success;
}
gneiss::engine::error application::action_map_status() const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  return has_asset_loading_service() ? storage_->action_map_result_
                                     : gneiss::engine::error::not_ready;
}
gneiss::engine::error application::cancel_action_map() noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (!has_asset_loading_service())
    return gneiss::engine::error::not_ready;
  storage_->cancel_action_map();
  return gneiss::engine::error::success;
}
gneiss::engine::error
application::find_action(std::string_view name,
                         gneiss::engine::action_id& out_action) const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  return storage_->input_.find_action(name, out_action);
}

gneiss::engine::error
application::get_action_state(gneiss::engine::action_id action,
                              gneiss::engine::action_state& out_state) const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  return storage_->input_.get_action_state(action, out_state);
}

gneiss::engine::error application::submit_ui_draw_list(const gneiss::engine::ui_draw_view& desc) {
  return storage_->is_updating_ ? storage_->ui_draw_list_.replace(desc, *storage_->render_service_)
                                : gneiss::engine::error::invalid_state;
}

gneiss::engine::error
application::submit_debug_draw_list(std::span<const gneiss::engine::debug_line> lines) {
  return storage_->is_updating_ ? storage_->debug_draw_list_.replace(lines)
                                : gneiss::engine::error::invalid_state;
}

void application::report(gneiss::engine::severity severity, gneiss::engine::category category,
                         gneiss::engine::error result, std::string_view module,
                         std::string_view message) {
  if (storage_->config_.callbacks.diagnostic != nullptr) {
    const gneiss::engine::diagnostic_view diagnostic{.level = severity,
                                                     .source = category,
                                                     .result = gneiss::engine::error{result},
                                                     .module = module,
                                                     .message = message};
    {
      storage_->config_.callbacks.diagnostic(diagnostic);
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
      .result = gneiss::engine::error{result},
  }));
}

gneiss::engine::error application::submit_log(const gneiss::engine::message_view& message) {
  return storage_->log_dispatcher_ == nullptr ? gneiss::engine::validate_message(message)
                                              : storage_->log_dispatcher_->submit(message);
}

gneiss::engine::error application::set_view_camera(const gneiss::engine::view_camera* camera) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (camera == nullptr) {
    storage_->view_camera_.reset();
    return gneiss::engine::error::success;
  }
  gneiss::engine::math::matrix4 matrix;
  auto result = gneiss::engine::math::view_matrix_rh(camera->transform, matrix);
  if (result == gneiss::engine::error::success) {
    result = gneiss::engine::math::perspective_rh_zo(camera->camera, 1.0F, matrix);
  }
  if (result == gneiss::engine::error::success) {
    storage_->view_camera_ = *camera;
  }
  return result;
}

gneiss::engine::error application::capture_frame(std::uint32_t width, std::uint32_t height,
                                                 gneiss::engine::frame_image& output) {
  output = {};
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (width == 0U || height == 0U || width > 1024U || height > 1024U) {
    return gneiss::engine::error::invalid_argument;
  }
  if (!storage_->window_service_) {
    return gneiss::engine::error::unsupported;
  }
  {
    auto window = storage_->window_service_->native_window();
    window.width = width;
    window.height = height;
    window.needs_recreate = false;
    auto snapshot = gneiss::engine::extract_render_snapshot(
        *storage_->world_.get(), width, height,
        storage_->view_camera_ ? &*storage_->view_camera_ : nullptr);
    if (!snapshot)
      return snapshot.error();
    gneiss::engine::render_frame_packet packet;
    auto result = gneiss::engine::capture_render_frame_packet(
        window, std::move(*snapshot), *storage_->render_service_, storage_->ui_draw_list_,
        storage_->debug_draw_list_, packet);
    if (result != gneiss::engine::error::success) {
      return result;
    }
    auto image = std::make_shared<gneiss::engine::frame_image>();
    packet.readback = image;
    auto submitted = storage_->render_service_->submit(
        std::move(packet), gneiss::engine::render_frame_policy::required);
    if (!submitted)
      return submitted.error();
    const auto sequence = *submitted;
    result = storage_->render_service_->finish_frames();
    if (result != gneiss::engine::error::success) {
      return result;
    }
    auto completed = storage_->render_service_->try_take_required_completion();
    if (!completed)
      return completed.error();
    if (!*completed || (*completed)->sequence != sequence ||
        (*completed)->status != gneiss::engine::error::success) {
      return gneiss::engine::error::internal;
    }
    output = std::move(*image);
    return gneiss::engine::error::success;
  }
}

gneiss::engine::error application::render_frame() {
  GNEISS_PROFILE_SCOPE("application.render.submit");
  if (storage_->window_service_ == nullptr) {
    return gneiss::engine::error::success;
  }
  auto& window = storage_->window_service_->native_window();
  if (window.width == 0U || window.height == 0U) {
    return gneiss::engine::error::success;
  }
  gneiss::engine::render_frame_packet packet;
  const auto storage_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::frame_storage, [&]() {
        return storage_->render_service_->prepare_frame_packet_storage(packet);
      });
  if (!storage_result)
    return storage_result.error();
  if (!*storage_result)
    return gneiss::engine::error::success;
  auto snapshot = gneiss::engine::measure(gneiss::engine::loop_stage::render_snapshot, [&]() {
    return gneiss::engine::extract_render_snapshot(
        *storage_->world_.get(), window.width, window.height,
        storage_->view_camera_ ? &*storage_->view_camera_ : nullptr);
  });
  if (!snapshot)
    return snapshot.error();
  const auto capture_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::frame_capture, [&]() {
        return gneiss::engine::capture_render_frame_packet(
            window, std::move(*snapshot), *storage_->render_service_, storage_->ui_draw_list_,
            storage_->debug_draw_list_, packet);
      });
  if (capture_result != gneiss::engine::error::success) {
    return capture_result;
  }
  const auto requested_recreate = window.needs_recreate;
  window.needs_recreate = false;
  const auto submit_result =
      gneiss::engine::measure(gneiss::engine::loop_stage::frame_submit, [&]() {
        return storage_->render_service_->submit(std::move(packet));
      });
  if (!submit_result && requested_recreate) {
    window.needs_recreate = true;
  }
  // 成功推进队列后先继续排空本地可做工作，队列繁忙且无进展时才等待。
  if (submit_result && storage_->loop_wakeup_) {
    storage_->loop_wakeup_->notify();
  }
  return submit_result ? gneiss::engine::error::success : submit_result.error();
}

gneiss::engine::error application::get_window_size(std::uint32_t& out_width,
                                                   std::uint32_t& out_height) const noexcept {
  if (storage_->window_service_ != nullptr) {
    const auto& window = storage_->window_service_->native_window();
    out_width = window.width;
    out_height = window.height;
    return gneiss::engine::error::success;
  }
  out_width = storage_->config_.window.width;
  out_height = storage_->config_.window.height;
  return gneiss::engine::error::success;
}

gneiss::engine::error application::run(std::uint64_t max_frame_count) try {
  if (!is_owner_thread() || !storage_->platform_initialized_ || storage_->is_running_) {
    return gneiss::engine::error::invalid_state;
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
    auto* progress = static_cast<gneiss::runtime::main_loop_wakeup*>(storage_->loop_wakeup_.get());
    const auto observed = progress != nullptr ? progress->snapshot() : 0U;
#endif
    bool should_close = false;
    const auto poll_result = gneiss::engine::measure(gneiss::engine::loop_stage::events,
                                                     [&] { return poll_events(should_close); });
    if (poll_result != gneiss::engine::error::success) {
      storage_->is_running_ = false;
      return poll_result;
    }
    if (should_close) {
      if (storage_->config_.callbacks.close_requested == nullptr ||
          storage_->config_.callbacks.close_requested()) {
        break;
      }
    }

    const auto advanced = advance_loading();
    if (advanced.failed()) {
      storage_->is_running_ = false;
      return advanced;
    }
    if (storage_->action_map_ready_ && storage_->action_map_ready_->load()) {
      auto loaded = storage_->pending_action_map_.take();
      storage_->action_map_result_ =
          loaded ? storage_->input_.replace_action_map(*(*loaded)->snapshot())
                 : loaded.error().error;
      storage_->pending_action_map_.cancel();
      storage_->action_map_ready_.reset();
    }
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
      if (update_result != gneiss::engine::error::success) {
        report(gneiss::engine::severity::error, gneiss::engine::category::application,
               update_result, "application", "更新回调失败");
        storage_->ui_draw_list_.clear();
        storage_->debug_draw_list_.clear();
        storage_->is_running_ = false;
        return update_result;
      }
    }
    const auto render_result =
        gneiss::engine::measure(gneiss::engine::loop_stage::render, [&] { return render_frame(); });
    // 渲染队列、交换链或后端资源可能暂时未就绪；跳过本帧并在下一帧重试。
    if (render_result != gneiss::engine::error::success &&
        render_result != gneiss::engine::error::not_ready) {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
      storage_->is_running_ = false;
      return render_result;
    }
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
  // 返回前回收已提交帧，避免短程运行把尚未回传的上传失败误报为成功。
  if (storage_->window_service_ != nullptr) {
    return storage_->render_service_->finish_frames();
  }
  return gneiss::engine::error::success;
} catch (...) {
  storage_->is_running_ = false;
  storage_->is_updating_ = false;
  storage_->ui_draw_list_.clear();
  storage_->debug_draw_list_.clear();
  throw;
}

bool application::is_owner_thread() const noexcept {
  return storage_->owner_thread_ == std::this_thread::get_id();
}

gneiss::engine::error application::request_shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_)
    return gneiss::engine::error::invalid_state;
  if (storage_->shutdown_requested_)
    return gneiss::engine::error::success;
  auto receipt = std::make_shared<storage::file_close_receipt>();
  auto files = storage_->files_;
  if (files) {
    const auto requested = files->shutdown([receipt](auto status) {
      receipt->result = status;
      receipt->ready = true;
    });
    if (requested.failed())
      return requested;
  } else {
    receipt->ready = true;
  }
  storage_->file_close_ = std::move(receipt);
  storage_->closing_files_ = std::move(files);
  storage_->shutdown_requested_ = true;
  storage_->accepts_requests_.store(false, std::memory_order_release);
  storage_->cancel_action_map();
  storage_->scene_service_.request_stop();
  if (storage_->asset_loading_)
    storage_->asset_loading_->request_stop();
  if (storage_->asset_manager_)
    storage_->asset_manager_->request_stop();
  return gneiss::engine::error::success;
}

gneiss::engine::error application::advance_shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_ ||
      !storage_->shutdown_requested_)
    return gneiss::engine::error::invalid_state;
  if (storage_->shutdown_complete_)
    return storage_->shutdown_result_;
  if (storage_->closing_files_) {
    const auto dispatched = storage_->closing_files_->dispatch_completions(64);
    if (dispatched.failed())
      return dispatched;
  }
  storage_->scene_service_.advance();
  if (storage_->asset_manager_)
    storage_->asset_manager_->advance();
  if (!storage_->scene_service_.stopped() ||
      (storage_->asset_manager_ && !storage_->asset_manager_->stopped()) ||
      !storage_->file_close_->ready)
    return gneiss::engine::error::not_ready;
  auto status = shutdown();
  if (status.ok())
    status = storage_->file_close_->result;
  storage_->closing_files_.reset();
  storage_->shutdown_complete_ = true;
  storage_->shutdown_result_ = status;
  return status;
}

gneiss::engine::error application::shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_) {
    return gneiss::engine::error::invalid_state;
  }
  storage_->shutting_down_ = true;
  storage_->accepts_requests_.store(false, std::memory_order_release);
  auto shutdown_result = gneiss::engine::error::success;
  storage_->cancel_action_map();
  storage_->asset_loading_.reset();
  storage_->scene_service_.shutdown();
  storage_->world_.reset();
  if (storage_->asset_manager_)
    storage_->asset_manager_->stop();
  storage_->asset_manager_.reset();
  storage_->is_updating_ = false;
  storage_->ui_draw_list_.clear();
  storage_->debug_draw_list_.clear();
  storage_->environment_.reset();
  storage_->files_.reset();
  if (storage_->platform_initialized_) {
    if (storage_->config_.callbacks.shutdown != nullptr) {
      {
        storage_->config_.callbacks.shutdown();
      }
    }
    storage_->platform_initialized_ = false;
  }
  if (storage_->window_service_ != nullptr) {
    const auto render_stats = storage_->render_service_->query_performance_stats();
    gneiss::engine::render_device_stats stats;
    const auto render_shutdown_result = storage_->render_service_->shutdown(stats);
    if (shutdown_result == gneiss::engine::error::success) {
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
           gneiss::engine::error::success, "granit.render.performance",
           std::string_view(performance_message.data(), performance_length));
    if (shutdown_result != gneiss::engine::error::success) {
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
  storage_->window_service_.reset();
#ifdef GNEISS_ASSET_LIBUV
  // 生产任务与已提交工作已经收尾，最后关闭文件执行器。
  storage_->asset_io_executor_.reset();
#endif
  storage_->render_service_->close_resources();
  storage_->shutting_down_ = false;
  return shutdown_result;
}

bool application::has_asset_loading_service() const noexcept {
  return storage_->asset_loading_.get() != nullptr;
}

engine::resource_load_group* application::loading_assets() noexcept {
  return storage_->asset_loading_.get();
}

scene_service& application::scenes() noexcept { return storage_->scene_service_; }

bool application::scene_loading() const noexcept { return storage_->scene_service_.loading(); }

engine::result<engine::asset_batch_request>
application::load_assets(std::span<const engine::render_resource_source> sources, bool reload) {
  if (!is_owner_thread() || !accepts_requests())
    return engine::error::invalid_state;
  auto* group = loading_assets();
  if (!group)
    return engine::error::not_ready;
  return group->begin(sources, reload,
                      !scene_loading() && !visual_assets_loading() &&
                          storage_->scene_service_.can_activate());
}
gneiss::engine::error application::advance_resource_io() {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::engine::error::invalid_state;
  if (const auto files = storage_->asset_manager_->async_files()) {
    const auto status = files->dispatch_completions(64);
    if (status.failed())
      return status;
  }
  storage_->asset_manager_->advance();
  storage_->asset_manager_->collect_unused();
  if (auto state = storage_->environment_; state && state->ready) {
    if (state->status.failed()) {
      storage_->environment_.reset();
      report(engine::severity::error, engine::category::asset, state->status, "environment",
             "环境资产异步读取失败");
      return state->status;
    }
    const auto submitted = storage_->render_service_->request_environment(
        state->bytes, storage_->config_.environment_intensity,
        storage_->config_.environment_rotation_radians);
    if (submitted != engine::error::not_ready)
      storage_->environment_.reset();
    if (submitted.failed() && submitted != engine::error::not_ready)
      return submitted;
  }
  return gneiss::engine::error::success;
}

engine::error application::advance_loading() {
  if (const auto status = advance_resource_io(); status.failed())
    return status;
  if (auto* assets = loading_assets())
    assets->advance();
  storage_->scene_service_.advance();
  report_visual_completion();
  return engine::error::success;
}

void application::report_visual_completion() {
  gneiss::engine::asset_load_completion completion;
  if (storage_->scene_service_.take_visual_completion(completion) &&
      completion.state != gneiss::engine::asset_load_state::cancelled) {
    for (const auto& item : completion.results) {
      if (!item.applied) {
        report(gneiss::engine::severity::error, gneiss::engine::category::asset, item.result,
               "scene.assets",
               completion.message.empty() ? item.source.uri
                                          : item.source.uri + ": " + completion.message);
      }
    }
  }
}

bool application::can_activate_scene() const noexcept {
  return storage_->scene_service_.can_activate() &&
         (!storage_->asset_loading_ || !storage_->asset_loading_->busy());
}

bool application::visual_assets_loading() const noexcept {
  return storage_->scene_service_.visual_loading();
}

bool application::accepts_requests() const noexcept {
  return storage_->accepts_requests_.load(std::memory_order_acquire);
}

gneiss::engine::world* application::native_world() const noexcept { return storage_->world_.get(); }

gneiss::engine::render_service& application::render() noexcept {
  return *storage_->render_service_;
}

std::shared_ptr<gneiss::engine::files::service> application::async_files() const noexcept {
  return storage_->files_;
}

gneiss::engine::asset_manager& application::assets() noexcept { return *storage_->asset_manager_; }

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
