// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/asset/detail/resource_load_group.hpp"
#include "function/scene/rendering/render_camera.hpp"
#include "function/scene/rendering/world_render_binding.hpp"
#include <atomic>
#include <gneiss/engine/core/diagnostics/loop_timing.hpp>
#include <gneiss/engine/core/diagnostics/profiling.hpp>
#include <gneiss/engine/core/logging/logger.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/utf8.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/input/service.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/render/frame_result.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/platform/files/service.hpp>
#include <optional>
#include <thread>
#include <tuple>
#ifdef GNEISS_NATIVE_LOOP_WAIT
#include "function/application/main_loop_wakeup.hpp"
#endif

#include <cmath>
#include <gneiss/engine/resource/asset_uri.hpp>
#ifdef __EMSCRIPTEN__
#include "gneiss/engine/platform/files/backends/browser.hpp"
#include "gneiss/engine/platform/files/backends/fetch.hpp"
#endif
#ifdef GNEISS_ASSET_LIBUV
#include "gneiss/engine/platform/files/backends/native.hpp"
#include <gneiss/engine/platform/io/executor.hpp>

#endif

#include <gneiss/engine/platform/window/service.hpp>

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
  std::shared_ptr<gneiss::wakeup_signal> loop_wakeup_;
  runtime::application_configuration config_;
  std::unique_ptr<gneiss::render::service> render_service_ =
      std::make_unique<gneiss::render::service>();
  gneiss::ui_draw_list ui_draw_list_;
  gneiss::debug_draw_list debug_draw_list_;
  std::vector<application_view> views_{application_view{}};
#ifdef GNEISS_ASSET_LIBUV
  std::unique_ptr<gneiss::io::executor> asset_io_executor_;
#endif
  std::shared_ptr<gneiss::files::service> files_;
  std::optional<error> files_initialization_;
  struct environment_read {
    bool ready{};
    gneiss::error status{gneiss::error::not_ready};
    std::shared_ptr<const std::vector<std::byte>> bytes;
  };
  std::shared_ptr<environment_read> environment_;
  std::unique_ptr<gneiss::asset_manager> asset_manager_;
  gneiss::asset_request<gneiss::input::action_map> pending_action_map_;
  std::shared_ptr<std::atomic<bool>> action_map_ready_;
  gneiss::error action_map_result_{gneiss::error::invalid_state};

  void cancel_action_map() noexcept {
    action_map_ready_.reset();
    pending_action_map_.cancel();
    action_map_result_ = gneiss::error::invalid_state;
  }
  gneiss::world_owner world_;
  std::unique_ptr<gneiss::world_render_binding> render_binding_;
  std::unique_ptr<gneiss::resource_load_group> asset_loading_;
  std::thread::id owner_thread_;
  std::uint64_t frame_index_ = 0;
  std::uint64_t elapsed_ns_ = 0;
  std::uint64_t previous_time_ns_ = 0;
  std::atomic<bool> accepts_requests_{false};
  bool shutting_down_ = false;
  bool shutdown_requested_{};
  bool shutdown_complete_{};
  gneiss::error shutdown_result_{gneiss::error::success};
  struct file_close_receipt {
    bool ready{};
    gneiss::error result{gneiss::error::success};
  };
  std::shared_ptr<file_close_receipt> file_close_;
  std::shared_ptr<gneiss::files::service> closing_files_;
  bool platform_initialized_ = false;
  bool is_running_ = false;
  bool is_paused_ = false;
  bool should_exit_ = false;
  bool is_updating_ = false;
  gneiss::input::service input_;
  std::unique_ptr<gneiss::window::service> window_service_;
  result<render::frame_request> make_render_request(const window::surface_state& surface,
                                                    render::target_description capture_size = {}) {
    auto synced = render_binding_->synchronize();
    if (!synced)
      return synced.error();
    render::frame_request request;
    request.surface = surface;
    bool has_window = false;
    for (const auto& view : views_) {
      render::target_description extent =
          capture_size.width && capture_size.height
              ? capture_size
              : render::target_description{surface.width, surface.height};
      if (view.target) {
        auto target = render_service_->describe_target(view.target);
        if (!target)
          return target.error();
        extent = *target;
      } else {
        if (!extent.width || !extent.height)
          continue;
        has_window = true;
      }
      auto camera = select_render_camera(*world_, extent.width, extent.height,
                                         view.camera ? &*view.camera : nullptr);
      if (!camera)
        return camera.error();
      if (*camera) {
        const auto& selected = **camera;
        request.views.push_back({.scene = render_binding_->scene(),
                                 .target = view.target,
                                 .view_matrix = selected.view,
                                 .projection_matrix = selected.projection,
                                 .camera_position = selected.transform.translation});
      } else {
        request.views.push_back({.target = view.target});
      }
    }
    if (!has_window &&
        ((surface.width && surface.height) || (capture_size.width && capture_size.height)))
      request.views.push_back({});
    request.ui = ui_draw_list_;
    request.debug = debug_draw_list_;
    return request;
  }
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
      timing_ = std::make_unique<gneiss::loop_timing>();
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
  void set_origin(gneiss::timing_clock::time_point origin) noexcept { origin_ = origin; }
  [[nodiscard]] gneiss::loop_timing* get() const noexcept { return timing_.get(); }

private:
  gneiss::timing_clock::time_point origin_;
  std::unique_ptr<std::ofstream> output_;
  std::unique_ptr<gneiss::loop_timing> timing_;
};
} // namespace

gneiss::result<std::shared_ptr<application>>
application::create(const application_configuration& config) {
  if ((config.enable_window &&
       (config.window.width == 0U || config.window.height == 0U ||
        config.callbacks.initialize != nullptr || config.callbacks.poll_events != nullptr ||
        config.callbacks.shutdown != nullptr)) ||
      !std::isfinite(config.environment_intensity) || config.environment_intensity < 0.0F ||
      !std::isfinite(config.environment_rotation_radians) ||
      (!config.environment_asset.empty() &&
       gneiss::validate_uri(config.environment_asset) != gneiss::error::success)) {
    return gneiss::error::invalid_argument;
  }
  {
    const auto meta_status = gneiss::initialize_meta();
    if (meta_status.failed())
      return meta_status;
    auto instance = std::shared_ptr<application>(new application(config));
    const auto result = instance->initialize(instance);
    if (result != gneiss::error::success) {
      return result;
    }
    instance->storage_->accepts_requests_.store(true, std::memory_order_release);
    return instance;
  }
}

application::application(const runtime::application_configuration& config)
    : storage_(std::make_unique<storage>(config)) {}

application::~application() noexcept { static_cast<void>(shutdown()); }

gneiss::render::queue_stats application::render_statistics() const {
  if (storage_->window_service_) {
    return storage_->render_service_->query_performance_stats();
  }
  return {};
}

gneiss::error application::attach_task_executor(gneiss::tasks::task_executor& executor) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  if (storage_->asset_loading_.get()) {
    return gneiss::error::invalid_state;
  }
  {
    gneiss::texture_prepare_profile profile;
    if (storage_->window_service_)
      profile = storage_->render_service_->texture_profile();
    const auto scope = executor.make_scope(storage_->loop_wakeup_);
    if (!scope.id)
      return gneiss::error::invalid_state;
    const auto configured = storage_->asset_manager_->configure(executor, profile, scope);
    if (configured.failed()) {
      (void)executor.close_scope(scope);
      return configured;
    }
    auto group = std::make_unique<gneiss::resource_load_group>(*storage_->asset_manager_);
    storage_->asset_loading_ = std::move(group);
    return gneiss::error::success;
  }
}

gneiss::error application::initialize(std::weak_ptr<application> self) {
  if (!storage_->render_service_->is_valid()) {
    return gneiss::error::out_of_memory;
  }
  {
    storage_->files_ = std::make_shared<gneiss::files::service>(
        gneiss::files::service_options{.max_file_bytes = 64U * 1024U * 1024U});
    for (const auto& [prefix, root, writable] :
         {std::tuple<std::string_view, std::string_view, bool>{
              "asset://", storage_->config_.asset_root, storage_->config_.writable_assets},
          {"user://", storage_->config_.user_root, true}}) {
      if (root.empty())
        continue;
      std::shared_ptr<gneiss::files::backend> backend;
#ifdef GNEISS_ASSET_LIBUV
      if (!storage_->asset_io_executor_) {
        storage_->asset_io_executor_ = std::make_unique<gneiss::io::executor>();
        const auto started = storage_->asset_io_executor_->start();
        if (started.failed())
          return started;
      }
      backend =
          gneiss::files::create_backend_native({*storage_->asset_io_executor_, std::string(root)});
#elif defined(__EMSCRIPTEN__)
      auto base = std::string(root);
      if (base.starts_with('/') && !base.starts_with("//")) {
        while (base.size() > 1 && base.ends_with('/'))
          base.pop_back();
        auto local = gneiss::files::create_backend_browser(
            {std::move(base), gneiss::files::browser_storage::existing});
        backend = std::move(local);
      } else {
        if (!base.ends_with('/'))
          base += '/';
        backend = gneiss::files::create_backend_fetch({std::move(base)});
      }
#else
      return gneiss::error::unsupported;
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
      storage_->window_service_ = std::make_unique<gneiss::window::service>();
    }
    const auto platform_result = storage_->window_service_->initialize(storage_->config_.window);
    if (platform_result != gneiss::error::success) {
      report(gneiss::severity::error, gneiss::category::backend, platform_result, "granit.platform",
             "Granit 平台初始化失败");
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
        storage_->window_service_->native(), storage_->window_service_->surface(), {},
        storage_->config_.environment_intensity, storage_->config_.environment_rotation_radians);
    if (render_result != gneiss::error::success) {
      report(gneiss::severity::error, gneiss::category::backend, render_result, "granit.render",
             "Granit 渲染服务初始化失败");
      storage_->window_service_.reset();
      return render_result;
    }
    storage_->render_service_->set_completion_wakeup(storage_->loop_wakeup_);
  }
  if (storage_->config_.callbacks.initialize != nullptr) {
    auto result = gneiss::error::success;
    {
      result = storage_->config_.callbacks.initialize();
    }
    if (result != gneiss::error::success) {
      report(gneiss::severity::error, gneiss::category::application, result,
             "application.initialize", "application 初始化回调失败");
      // 初始化回调可能已获得部分资源，因此失败时也执行配对清理。
      storage_->platform_initialized_ = true;
      static_cast<void>(shutdown());
      return result;
    }
  }
  storage_->platform_initialized_ = true;

  try {
    storage_->asset_manager_ = std::make_unique<gneiss::asset_manager>(storage_->files_);
    if (storage_->asset_manager_->attach_render(*storage_->render_service_).failed())
      return gneiss::error::invalid_state;
    auto created_world = gneiss::create_native();
    if (!created_world)
      return created_world.error();
    storage_->world_ = std::move(*created_world);
    if (storage_->window_service_) {
      auto binding = world_render_binding::create(*storage_->world_, *storage_->render_service_);
      if (!binding)
        return binding.error();
      storage_->render_binding_ = std::move(*binding);
    }
  } catch (...) {
    static_cast<void>(shutdown());
    throw;
  }
  // 只接收服务级完成通知；不轮询各个后端。环境加载在文件服务可用后启动。
  const auto accepted = storage_->files_->initialize([self](error status) {
    auto owner = self.lock();
    if (!owner || !owner->storage_->files_)
      return;
    auto& application_state = owner->storage_;
    application_state->files_initialization_ = status;
    if (status.failed())
      return;
    if (application_state->window_service_ &&
        !application_state->config_.environment_asset.empty()) {
      {
        auto state = std::make_shared<storage::environment_read>();
        gneiss::files::request_id request;
        const auto accepted = application_state->files_->read(
            application_state->config_.environment_asset, {.max_bytes = 64U * 1024U * 1024U},
            [state](gneiss::files::completion<gneiss::files::bytes> value) {
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
        if (accepted.failed()) {
          application_state->files_initialization_ = accepted;
          return;
        }
        application_state->environment_ = std::move(state);
      }
    }
  });
  if (accepted.failed())
    return accepted;
  // 内联完成的后端也经同一队列通知；异步后端由后续主循环继续分发，不阻塞等待。
  const auto dispatched = storage_->files_->dispatch_completions(64);
  if (dispatched.failed())
    return dispatched;
  return storage_->files_initialization_.value_or(error::success);
}

std::uint64_t application::now_ns() const {
  if (storage_->config_.callbacks.now_ns != nullptr) {
    return storage_->config_.callbacks.now_ns();
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

gneiss::error application::poll_events(bool& out_should_close) {
  GNEISS_PROFILE_SCOPE("application.events");
  out_should_close = false;
  if (storage_->window_service_ != nullptr) {
    storage_->input_.begin_frame();
    const auto events = storage_->window_service_->poll_events([&](const input::event& event) {
      return storage_->input_.push(event) ? error::success : error::invalid_argument;
    });
    if (!events)
      return events.error();
    out_should_close = events->close_requested;
    if (events->focus_lost)
      storage_->input_.clear_focus();
    return error::success;
  }
  if (storage_->config_.callbacks.poll_events == nullptr) {
    return gneiss::error::success;
  }
  {
    return storage_->config_.callbacks.poll_events(out_should_close);
  }
}

gneiss::error application::poll_input(gneiss::input::event& out_event) noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  return storage_->input_.poll(out_event);
}

gneiss::error application::begin_action_map(std::string_view uri) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  if (!has_asset_loading_service())
    return gneiss::error::not_ready;
  storage_->cancel_action_map();
  auto requested = storage_->asset_manager_->reload<gneiss::input::action_map>(uri);
  if (!requested)
    return requested.error();
  storage_->pending_action_map_ = std::move(*requested);
  storage_->action_map_result_ = gneiss::error::not_ready;
  storage_->action_map_ready_ = std::make_shared<std::atomic<bool>>(false);
  // 每个请求独立的完成通知；取消或替换后，旧通知不能影响当前请求。
  gneiss::detail::asset_request_access::when_ready(
      storage_->pending_action_map_, [ready = std::weak_ptr{storage_->action_map_ready_}] {
        if (auto receipt = ready.lock())
          receipt->store(true);
      });
  return gneiss::error::success;
}
gneiss::error application::action_map_status() const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  return has_asset_loading_service() ? storage_->action_map_result_ : gneiss::error::not_ready;
}
gneiss::error application::cancel_action_map() noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  if (!has_asset_loading_service())
    return gneiss::error::not_ready;
  storage_->cancel_action_map();
  return gneiss::error::success;
}
gneiss::error application::find_action(std::string_view name,
                                       gneiss::input::action_id& out_action) const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  return storage_->input_.find_action(name, out_action);
}

gneiss::error application::get_action_state(gneiss::input::action_id action,
                                            gneiss::input::action_state& out_state) const noexcept {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  return storage_->input_.get_action_state(action, out_state);
}

gneiss::error application::submit_ui_draw_list(const gneiss::ui_draw_view& desc) {
  return storage_->is_updating_ ? storage_->ui_draw_list_.replace(desc, *storage_->render_service_)
                                : gneiss::error::invalid_state;
}

gneiss::error application::submit_debug_draw_list(std::span<const gneiss::debug_line> lines) {
  return storage_->is_updating_ ? storage_->debug_draw_list_.replace(lines)
                                : gneiss::error::invalid_state;
}

void application::report(gneiss::severity severity, gneiss::category category, gneiss::error result,
                         std::string_view module, std::string_view message) {
  if (storage_->config_.callbacks.diagnostic != nullptr) {
    const gneiss::diagnostic_view diagnostic{.level = severity,
                                             .source = category,
                                             .result = gneiss::error{result},
                                             .module = module,
                                             .message = message};
    {
      storage_->config_.callbacks.diagnostic(diagnostic);
    }
  }
  const auto log_severity = severity == gneiss::severity::info ? gneiss::logging::level::info
                            : severity == gneiss::severity::warning
                                ? gneiss::logging::level::warning
                                : gneiss::logging::level::error;
  const std::string_view log_category = category == gneiss::category::asset     ? "asset"
                                        : category == gneiss::category::input   ? "input"
                                        : category == gneiss::category::backend ? "backend"
                                                                                : "application";
  static_cast<void>(gneiss::logging::logger{"application"}.write({
      .severity = log_severity,
      .source = module,
      .category = log_category,
      .message = message,
      .result = gneiss::error{result},
  }));
}

result<void> application::set_views(std::span<const application_view> views) {
  if (!is_owner_thread() || !accepts_requests())
    return error::invalid_state;
  for (std::size_t i = 0; i < views.size(); ++i) {
    const auto& view = views[i];
    if (view.target && !render().is_target(view.target))
      return error::invalid_handle;
    for (std::size_t j = 0; j < i; ++j) {
      if (views[j].target == view.target)
        return error::invalid_argument;
    }
    if (view.camera) {
      math::matrix4 matrix;
      auto status = math::view_matrix_rh(view.camera->transform, matrix);
      if (status.failed())
        return status;
      status = math::perspective_rh_zo(view.camera->camera, 1.0F, matrix);
      if (status.failed())
        return status;
    }
  }
  std::vector<application_view> replacement(views.begin(), views.end());
  if (replacement.empty())
    replacement.emplace_back();
  storage_->views_ = std::move(replacement);
  return {};
}

gneiss::error application::set_view_target(rid target) {
  if (!is_owner_thread() || !accepts_requests())
    return error::invalid_state;
  auto views = storage_->views_;
  views.front().target = target;
  auto status = set_views(views);
  return status ? error::success : status.error();
}

gneiss::error application::set_view_camera(const gneiss::view_camera* camera) {
  if (!is_owner_thread() || !accepts_requests())
    return error::invalid_state;
  auto views = storage_->views_;
  views.front().camera = camera ? std::optional{*camera} : std::nullopt;
  auto status = set_views(views);
  return status ? error::success : status.error();
}

gneiss::error application::capture_frame(std::uint32_t width, std::uint32_t height,
                                         gneiss::render::frame_image& output) {
  output = {};
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  if (width == 0U || height == 0U || width > 1024U || height > 1024U) {
    return gneiss::error::invalid_argument;
  }
  if (!storage_->window_service_) {
    return gneiss::error::unsupported;
  }
  {
    const auto window = storage_->window_service_->surface();
    storage_->world_->flush_changes();
    auto request = storage_->make_render_request(window, {width, height});
    if (!request)
      return request.error();
    auto image = std::make_shared<gneiss::render::frame_image>();
    auto submitted =
        storage_->render_service_->request_capture(std::move(*request), image, {width, height});
    if (!submitted)
      return submitted.error();
    const auto sequence = *submitted;
    auto result = storage_->render_service_->finish_frames();
    if (result != gneiss::error::success) {
      return result;
    }
    auto completed = storage_->render_service_->try_take_required_completion();
    if (!completed)
      return completed.error();
    if (!*completed || (*completed)->sequence != sequence)
      return gneiss::error::internal;
    if ((*completed)->status.failed())
      return (*completed)->status;
    output = std::move(*image);
    return gneiss::error::success;
  }
}

gneiss::error application::render_frame() {
  GNEISS_PROFILE_SCOPE("application.render.submit");
  if (storage_->window_service_ == nullptr) {
    return gneiss::error::success;
  }
  const auto window = storage_->window_service_->surface();
  if ((window.width == 0U || window.height == 0U) &&
      std::ranges::none_of(storage_->views_, [](const auto& view) { return bool(view.target); })) {
    auto synced = storage_->render_binding_->synchronize();
    return synced ? error{error::success} : synced.error();
  }
  auto request = storage_->make_render_request(window);
  if (!request)
    return request.error();
  const auto submitted = measure(loop_stage::frame_submit, [&] {
    return storage_->render_service_->render(std::move(*request));
  });
  if (!submitted)
    return submitted.error() == error::not_ready ? error{error::success} : submitted.error();
  if (storage_->loop_wakeup_)
    storage_->loop_wakeup_->notify();
  return error::success;
}

gneiss::error application::get_window_size(std::uint32_t& out_width,
                                           std::uint32_t& out_height) const noexcept {
  if (storage_->window_service_ != nullptr) {
    const auto& window = storage_->window_service_->surface();
    out_width = window.width;
    out_height = window.height;
    return gneiss::error::success;
  }
  out_width = storage_->config_.window.width;
  out_height = storage_->config_.window.height;
  return gneiss::error::success;
}

gneiss::error application::run(std::uint64_t max_frame_count) try {
  if (!is_owner_thread() || !storage_->platform_initialized_ || storage_->is_running_) {
    return gneiss::error::invalid_state;
  }
  storage_->is_running_ = true;
  storage_->should_exit_ = false;
  loop_trace_file trace;
  const auto trace_origin =
      trace.get() != nullptr ? gneiss::timing_clock::now() : gneiss::timing_clock::time_point{};
  trace.set_origin(trace_origin);
  storage_->previous_time_ns_ = now_ns();
  std::uint64_t frames_run = 0;
  GNEISS_PROFILE_THREAD("Gneiss main");

  while (!storage_->should_exit_ && (max_frame_count == 0U || frames_run < max_frame_count)) {
    GNEISS_PROFILE_FRAME();
    GNEISS_PROFILE_SCOPE("application.loop");
    const gneiss::loop_frame frame_trace(trace.get(), storage_->frame_index_, trace_origin);
#ifdef GNEISS_NATIVE_LOOP_WAIT
    auto* progress = static_cast<gneiss::runtime::main_loop_wakeup*>(storage_->loop_wakeup_.get());
    const auto observed = progress != nullptr ? progress->snapshot() : 0U;
#endif
    bool should_close = false;
    const auto poll_result =
        gneiss::measure(gneiss::loop_stage::events, [&] { return poll_events(should_close); });
    if (poll_result != gneiss::error::success) {
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
    if (advanced == error::not_ready && !storage_->files_initialization_) {
      // 启动尚未完成，仍允许下一轮窗口事件与文件完成分发，不执行业务更新。
      ++frames_run;
      continue;
    }
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
    const gneiss::frame_time time = {
        .frame_index = storage_->frame_index_,
        .delta_ns = delta_ns,
        .elapsed_ns = storage_->elapsed_ns_,
        .is_paused = storage_->is_paused_,
    };
    gneiss::measure(gneiss::loop_stage::draw_clear, [&] {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
    });
    if (storage_->config_.callbacks.update != nullptr) {
      storage_->is_updating_ = true;
      const auto update_result = gneiss::measure(gneiss::loop_stage::update, [&] {
        GNEISS_PROFILE_SCOPE("application.update");
        return storage_->config_.callbacks.update(time);
      });
      storage_->is_updating_ = false;
      if (update_result != gneiss::error::success) {
        report(gneiss::severity::error, gneiss::category::application, update_result, "application",
               "更新回调失败");
        storage_->ui_draw_list_.clear();
        storage_->debug_draw_list_.clear();
        storage_->is_running_ = false;
        return update_result;
      }
    }
    storage_->world_->flush_changes();
    const auto render_result =
        gneiss::measure(gneiss::loop_stage::render, [&] { return render_frame(); });
    // 渲染队列、交换链或后端资源可能暂时未就绪；跳过本帧并在下一帧重试。
    if (render_result != gneiss::error::success && render_result != gneiss::error::not_ready) {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
      storage_->is_running_ = false;
      return render_result;
    }
    gneiss::measure(gneiss::loop_stage::draw_clear, [&] {
      storage_->ui_draw_list_.clear();
      storage_->debug_draw_list_.clear();
    });
    ++storage_->frame_index_;
    ++frames_run;
#ifdef GNEISS_NATIVE_LOOP_WAIT
    if (progress != nullptr && !storage_->should_exit_ &&
        (max_frame_count == 0U || frames_run < max_frame_count)) {
      gneiss::measure(gneiss::loop_stage::idle_wait, [&] {
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
  return gneiss::error::success;
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

gneiss::error application::request_shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_)
    return gneiss::error::invalid_state;
  if (storage_->shutdown_requested_)
    return gneiss::error::success;
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
  if (storage_->asset_loading_)
    storage_->asset_loading_->request_stop();
  if (storage_->asset_manager_)
    storage_->asset_manager_->request_stop();
  return gneiss::error::success;
}

gneiss::error application::advance_shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_ ||
      !storage_->shutdown_requested_)
    return gneiss::error::invalid_state;
  if (storage_->shutdown_complete_)
    return storage_->shutdown_result_;
  if (storage_->closing_files_) {
    const auto dispatched = storage_->closing_files_->dispatch_completions(64);
    if (dispatched.failed())
      return dispatched;
  }
  if (storage_->asset_manager_)
    storage_->asset_manager_->advance();
  if ((storage_->asset_manager_ && !storage_->asset_manager_->stopped()) ||
      !storage_->file_close_->ready)
    return gneiss::error::not_ready;
  auto status = shutdown();
  if (status.ok())
    status = storage_->file_close_->result;
  storage_->closing_files_.reset();
  storage_->shutdown_complete_ = true;
  storage_->shutdown_result_ = status;
  return status;
}

gneiss::error application::shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_) {
    return gneiss::error::invalid_state;
  }
  storage_->shutting_down_ = true;
  storage_->accepts_requests_.store(false, std::memory_order_release);
  auto shutdown_result = gneiss::error::success;
  storage_->cancel_action_map();
  storage_->asset_loading_.reset();
  storage_->render_binding_.reset();
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
    gneiss::render::device_stats stats;
    const auto render_shutdown_result = storage_->render_service_->shutdown(stats);
    if (shutdown_result == gneiss::error::success) {
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
    report(gneiss::severity::info, gneiss::category::backend, gneiss::error::success,
           "granit.render.performance",
           std::string_view(performance_message.data(), performance_length));
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
      const auto length = written > 0 ? std::min<std::size_t>(static_cast<std::size_t>(written),
                                                              message.size() - 1U)
                                      : 0U;
      report(gneiss::severity::error, gneiss::category::backend, shutdown_result,
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

gneiss::resource_load_group* application::loading_assets() noexcept {
  return storage_->asset_loading_.get();
}

gneiss::result<gneiss::asset_batch_request>
application::load_assets(std::span<const gneiss::render_resource_source> sources, bool reload) {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  auto* group = loading_assets();
  if (!group)
    return gneiss::error::not_ready;
  return group->begin(sources, reload);
}
gneiss::error application::advance_resource_io() {
  if (!is_owner_thread() || !accepts_requests())
    return gneiss::error::invalid_state;
  if (const auto files = storage_->asset_manager_->async_files()) {
    const auto status = files->dispatch_completions(64);
    if (status.failed())
      return status;
  }
  if (!storage_->files_initialization_)
    return error::not_ready;
  if (storage_->files_initialization_->failed())
    return *storage_->files_initialization_;
  storage_->asset_manager_->advance();
  storage_->asset_manager_->collect_unused();
  if (auto state = storage_->environment_; state && state->ready) {
    if (state->status.failed()) {
      storage_->environment_.reset();
      report(gneiss::severity::error, gneiss::category::asset, state->status, "environment",
             "环境资产异步读取失败");
      return state->status;
    }
    const auto submitted = storage_->render_service_->request_environment(
        state->bytes, storage_->config_.environment_intensity,
        storage_->config_.environment_rotation_radians);
    if (submitted != gneiss::error::not_ready)
      storage_->environment_.reset();
    if (submitted.failed() && submitted != gneiss::error::not_ready)
      return submitted;
  }
  return gneiss::error::success;
}

gneiss::error application::advance_loading() {
  if (const auto status = advance_resource_io(); status.failed())
    return status;
  if (auto* assets = loading_assets())
    assets->advance();
  return gneiss::error::success;
}

bool application::can_load_assets() const noexcept {
  return accepts_requests() && storage_->asset_loading_ && !storage_->asset_loading_->busy();
}

bool application::accepts_requests() const noexcept {
  return storage_->accepts_requests_.load(std::memory_order_acquire);
}

gneiss::world* application::native_world() const noexcept { return storage_->world_.get(); }

gneiss::render::service& application::render() noexcept { return *storage_->render_service_; }

std::shared_ptr<gneiss::files::service> application::async_files() const noexcept {
  return storage_->files_;
}

gneiss::asset_manager& application::assets() noexcept { return *storage_->asset_manager_; }

const gneiss::input::keyboard_state& application::keyboard_state() const noexcept {
  return storage_->input_.keyboard();
}

const gneiss::input::pointer_state& application::pointer_state() const noexcept {
  return storage_->input_.pointer();
}

bool application::can_submit_draw_lists() const noexcept { return storage_->is_updating_; }

void application::request_exit() noexcept { storage_->should_exit_ = true; }

void application::set_paused(bool value) noexcept { storage_->is_paused_ = value; }

} // namespace gneiss::runtime
