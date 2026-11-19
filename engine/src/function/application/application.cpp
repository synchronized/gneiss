// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <atomic>
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/base/utf8.hpp>
#include <gneiss/engine/core/diagnostics/profiling.hpp>
#include <gneiss/engine/core/logging/logger.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/diagnostics/loop_timing.hpp>
#include <gneiss/engine/function/files/service.hpp>
#include <gneiss/engine/function/input/service.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/render/frame_result.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <optional>
#include <thread>
#include <tuple>
#ifdef GNEISS_NATIVE_LOOP_WAIT
#include "function/application/main_loop_wakeup.hpp"
#endif

#include <cmath>
#include <gneiss/engine/resource/asset_uri.hpp>
#ifdef __EMSCRIPTEN__
#include "gneiss/engine/function/files/backends/browser.hpp"
#include "gneiss/engine/function/files/backends/fetch.hpp"
#endif
#ifdef GNEISS_ASSET_LIBUV
#include "gneiss/engine/function/files/backends/native.hpp"
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

namespace {
void log_error(std::string_view category, gneiss::error result, std::string_view source,
               std::string_view message) {
  gneiss::logging::logger{"application"}.write({.severity = gneiss::logging::level::error,
                                                .source = source,
                                                .category = category,
                                                .message = message,
                                                .result = result});
}
}

namespace gneiss::app {
struct system_failure {
  error code;
  std::string_view name; // 借用已冻结调度表，不能超过其有效期。
};
/** 所属线程使用；首次运行前冻结。捕获的借用对象须覆盖全部运行及关闭过程。 */
class schedule {
public:
  void add(stage phase, std::string name, system_function function,
           std::optional<detail::state_key> condition = {}) {
    GNEISS_ASSERT(function, "系统不能为空");
    check_registration(name);
    entries_.push_back({phase, std::move(name), std::move(function), condition});
  }

  void add_start(std::string name, system_function function) {
    GNEISS_ASSERT(function, "启动系统不能为空");
    check_registration(name);
    starts_.push_back({std::move(name), std::move(function)});
  }
  result<void, system_failure> start(const frame_time& time, const bool& stopping) {
    GNEISS_ASSERT(frozen_, "运行前必须冻结调度表");
    while (next_start_ < starts_.size() && !stopping) {
      const auto& item = starts_[next_start_++];
      const auto status = item.function(time);
      if (status.failed())
        return system_failure{status, item.name};
    }
    return {};
  }
  void add_stop(std::string name, stop_function function) {
    GNEISS_ASSERT(function, "停止系统不能为空");
    check_registration(name);
    stops_.push_back({std::move(name), std::move(function)});
  }
  template <class Report> bool stop(Report report) {
    frozen_ = true;
    while (next_stop_ < stops_.size()) {
      const auto& item = stops_[next_stop_++];
      auto status = item.function();
      if (!status)
        report(system_failure{status.error(), item.name});
      else if (*status == stop_progress::pending) {
        --next_stop_;
        return false;
      }
    }
    return true;
  }
  void init(detail::state_key initial) {
    GNEISS_ASSERT(!frozen_, "状态须在运行前注册");
    for (const auto& state : states_)
      GNEISS_ASSERT(state.current.type != initial.type, "状态类型只能注册一次");
    states_.push_back({initial, {}, false});
  }
  void add_hook(detail::state_event event, detail::state_key key, std::string name,
                system_function function) {
    (void)find(key.type);
    if (event == detail::state_event::update) {
      add(stage::update, std::move(name), std::move(function), key);
      return;
    }
    GNEISS_ASSERT(function, "系统不能为空");
    check_registration(name);
    hooks_.push_back({event, key, std::move(name), std::move(function)});
  }
  result<void> request(detail::state_key key) {
    auto& state = find(key.type);
    if (state.pending && *state.pending != key.value)
      return error::invalid_state;
    state.pending = key.value;
    return {};
  }
  std::uint64_t current(type_id type) const { return find(type).current.value; }
  void freeze() noexcept { frozen_ = true; }
  result<void, system_failure> transition(const frame_time& time, const bool& stopping) {
    GNEISS_ASSERT(frozen_, "运行前必须冻结调度表");
    if (transition_failure_)
      return *transition_failure_;
    // 一次性取走全部待处理请求：钩子对其他状态类型的新请求也留至下一轮。
    std::vector<std::optional<std::uint64_t>> requests;
    requests.reserve(states_.size());
    for (auto& state : states_)
      requests.push_back(std::exchange(state.pending, {}));
    for (std::size_t i = 0; i < states_.size() && !stopping; ++i) {
      auto& state = states_[i];
      if (!state.entered) {
        state.entered = true;
        auto entered = invoke(detail::state_event::enter, state.current, time, stopping);
        if (!entered)
          return entered;
        // 初始 Enter 不被首次转换省略；预先请求的目标仍在本轮继续转换。
      }
      if (stopping || !requests[i] || *requests[i] == state.current.value)
        continue;
      auto exited = invoke(detail::state_event::exit, state.current, time, stopping);
      if (!exited)
        return exited;
      if (stopping)
        break;
      state.current.value = *requests[i];
      auto entered = invoke(detail::state_event::enter, state.current, time, stopping);
      if (!entered)
        return entered;
    }
    return {};
  }
  result<void, system_failure> run(stage phase, const frame_time& time,
                                   const bool& stopping) const {
    GNEISS_ASSERT(frozen_, "运行前必须冻结调度表");
    for (const auto& item : entries_) {
      if (stopping)
        break;
      if (item.phase != phase ||
          (item.condition && find(item.condition->type).current != *item.condition))
        continue;
      auto status = item.function(time);
      if (status.failed())
        return system_failure{status, item.name};
    }
    return {};
  }

private:
  struct entry {
    stage phase;
    std::string name;
    system_function function;
    std::optional<detail::state_key> condition;
  };
  struct hook {
    detail::state_event event;
    detail::state_key key;
    std::string name;
    system_function function;
  };
  struct state_slot {
    detail::state_key current;
    std::optional<std::uint64_t> pending;
    bool entered;
  };
  const state_slot& find(type_id type) const {
    for (const auto& state : states_)
      if (state.current.type == type)
        return state;
    GNEISS_PANIC("业务状态类型尚未注册");
  }
  state_slot& find(type_id type) {
    return const_cast<state_slot&>(std::as_const(*this).find(type));
  }
  void check_registration(const std::string& name) const {
    GNEISS_ASSERT(!frozen_ && !name.empty(), "系统须在运行前具名注册");
    for (const auto& item : entries_)
      GNEISS_ASSERT(item.name != name, "系统名称不能重复");
    for (const auto& item : hooks_)
      GNEISS_ASSERT(item.name != name, "系统名称不能重复");
    for (const auto& item : starts_)
      GNEISS_ASSERT(item.name != name, "系统名称不能重复");
    for (const auto& item : stops_)
      GNEISS_ASSERT(item.name != name, "系统名称不能重复");
  }
  result<void, system_failure> invoke(detail::state_event event, detail::state_key key,
                                      const frame_time& time, const bool& stopping) {
    for (const auto& item : hooks_) {
      if (stopping)
        break;
      if (item.event != event || item.key != key)
        continue;
      auto status = item.function(time);
      if (status.failed()) {
        // 转换不是事务；失败后不能在再次 run 时重放已产生副作用的 Enter/Exit。
        transition_failure_ = system_failure{status, item.name};
        return *transition_failure_;
      }
    }
    return {};
  }
  std::vector<entry> entries_;
  struct start_entry {
    std::string name;
    system_function function;
  };
  std::vector<start_entry> starts_;
  std::size_t next_start_{};
  struct stop_entry {
    std::string name;
    stop_function function;
  };
  std::vector<stop_entry> stops_;
  std::size_t next_stop_{};
  std::vector<hook> hooks_;
  std::vector<state_slot> states_;
  std::optional<system_failure> transition_failure_;
  bool frozen_{};
};

}

namespace gneiss::runtime {
struct application::storage {
  explicit storage(const runtime::application_configuration& config)
      : config_(config), window_title_(config.window.title), asset_root_(config.asset_root),
        user_root_(config.user_root), owner_thread_(std::this_thread::get_id()) {
    config_.window.title = window_title_;
    config_.asset_root = asset_root_;
    config_.user_root = user_root_;
  }
  std::shared_ptr<gneiss::wakeup_signal> loop_wakeup_;
  runtime::application_configuration config_;
  // Application 跨帧保存配置；后端或完成回调不能借用调用方的临时字符串。
  std::string window_title_;
  std::string asset_root_;
  std::string user_root_;
  std::unique_ptr<gneiss::render::service> render_service_ =
      std::make_unique<gneiss::render::service>();
  app::schedule systems_;
#ifdef GNEISS_ASSET_LIBUV
  std::unique_ptr<gneiss::io::executor> asset_io_executor_;
#endif
  std::shared_ptr<gneiss::files::service> files_;
  std::optional<error> files_initialization_;
  std::unique_ptr<gneiss::asset_manager> asset_manager_;
  std::thread::id owner_thread_;
  std::uint64_t frame_index_ = 0;
  std::uint64_t elapsed_ns_ = 0;
  std::uint64_t previous_time_ns_ = 0;
  std::atomic<bool> accepts_requests_{false};
  bool shutting_down_ = false;
  bool stopping_host_{};
  error host_stop_result_{error::success};
  bool shutdown_requested_{};
  bool shutdown_complete_{};
  gneiss::error shutdown_result_{gneiss::error::success};
  struct file_close_receipt {
    bool ready{};
    gneiss::error result{gneiss::error::success};
  };
  std::shared_ptr<file_close_receipt> file_close_;
  std::shared_ptr<gneiss::files::service> closing_files_;
  app::lifecycle lifecycle_{app::lifecycle::starting};
  error lifecycle_result_{error::success};
  bool step_started_{};
  bool step_active_{};
  bool platform_initialized_ = false;
  bool is_running_ = false;
  bool should_exit_ = false;
  gneiss::input::service input_;
  std::unique_ptr<gneiss::window::service> window_service_;
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
  if (config.enable_window &&
      (config.window.width == 0U || config.window.height == 0U ||
       config.callbacks.initialize != nullptr || config.callbacks.poll_events != nullptr ||
       config.callbacks.shutdown != nullptr)) {
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
    if (config.executor) {
      const auto configured = instance->configure_executor(*config.executor);
      if (configured.failed())
        return configured;
    }
    instance->storage_->accepts_requests_.store(true, std::memory_order_release);
    return instance;
  }
}

application::application(const runtime::application_configuration& config)
    : storage_(std::make_unique<storage>(config)) {}

application::~application() noexcept { static_cast<void>(shutdown()); }

void application::register_system(app::stage phase, std::string name,
                                  app::system_function function) {
  GNEISS_ASSERT(is_owner_thread() && accepts_requests() && !storage_->is_running_,
                "系统仅允许所属线程在运行前注册");
  storage_->systems_.add(phase, std::move(name), std::move(function));
}

void application::register_start_system(std::string name, app::system_function function) {
  GNEISS_ASSERT(is_owner_thread() && accepts_requests() && !storage_->is_running_,
                "启动系统仅允许所属线程在运行前注册");
  storage_->systems_.add_start(std::move(name), std::move(function));
}

void application::register_stop_system(std::string name, app::stop_function function) {
  GNEISS_ASSERT(is_owner_thread() && accepts_requests() && !storage_->is_running_,
                "停止系统仅允许所属线程在运行前注册");
  storage_->systems_.add_stop(std::move(name), std::move(function));
}
bool application::stop_host() {
  storage_->accepts_requests_.store(false, std::memory_order_release);
  storage_->stopping_host_ = true;
  try {
    const bool complete = storage_->systems_.stop([&](const app::system_failure& failure) {
      if (storage_->host_stop_result_.ok())
        storage_->host_stop_result_ = failure.code;
      log_error("application", failure.code, failure.name, "宿主停止系统失败，继续释放其余资源");
    });
    storage_->stopping_host_ = false;
    return complete;
  } catch (...) {
    storage_->stopping_host_ = false;
    throw;
  }
}

void application::register_state(app::detail::state_key initial) {
  GNEISS_ASSERT(is_owner_thread() && accepts_requests() && !storage_->is_running_,
                "状态仅允许所属线程在运行前注册");
  storage_->systems_.init(initial);
}
void application::register_state_system(app::detail::state_event event, app::detail::state_key key,
                                        std::string name, app::system_function function) {
  GNEISS_ASSERT(is_owner_thread() && accepts_requests() && !storage_->is_running_,
                "状态系统仅允许所属线程在运行前注册");
  storage_->systems_.add_hook(event, key, std::move(name), std::move(function));
}
result<void> application::request_state_change(app::detail::state_key target) {
  GNEISS_ASSERT(is_owner_thread(), "状态转换仅允许所属线程请求");
  if (!accepts_requests() || storage_->should_exit_)
    return error::cancelled;
  return storage_->systems_.request(target);
}
std::uint64_t application::current_state(type_id type) const {
  GNEISS_ASSERT(is_owner_thread(), "状态查询仅允许所属线程");
  return storage_->systems_.current(type);
}

error application::configure_executor(tasks::executor& executor) {
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
      log_error("backend", platform_result, "granit.platform", "Granit 平台初始化失败");
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
        storage_->window_service_->native(), storage_->window_service_->surface());
    if (render_result != gneiss::error::success) {
      log_error("backend", render_result, "granit.render", "Granit 渲染服务初始化失败");
      storage_->window_service_.reset();
      return render_result;
    }
    storage_->render_service_->set_completion_wakeup(storage_->loop_wakeup_);
  }
  // 初始化可能先取得部分资源再抛出异常；进入回调前就登记配对清理。
  storage_->platform_initialized_ = true;
  if (storage_->config_.callbacks.initialize != nullptr) {
    auto result = gneiss::error::success;
    {
      result = storage_->config_.callbacks.initialize();
    }
    if (result != gneiss::error::success) {
      log_error("application", result, "application.initialize", "application 初始化回调失败");
      // 初始化回调可能已获得部分资源，因此失败时也执行配对清理。
      static_cast<void>(shutdown());
      return result;
    }
  }

  try {
    storage_->asset_manager_ = std::make_unique<gneiss::asset_manager>(storage_->files_);
    if (storage_->asset_manager_->attach_render(*storage_->render_service_).failed())
      return gneiss::error::invalid_state;

  } catch (...) {
    static_cast<void>(shutdown());
    throw;
  }
  // 只接收服务级完成通知；业务资产由宿主系统加载。
  const auto accepted = storage_->files_->initialize([self](error status) {
    auto owner = self.lock();
    if (!owner || !owner->storage_->files_)
      return;
    auto& application_state = owner->storage_;
    application_state->files_initialization_ = status;
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

const gneiss::window::service* application::window() const noexcept {
  return storage_->window_service_.get();
}

error application::dispatch_completion_systems() {
  const auto completed = storage_->systems_.run(
      app::stage::completions,
      {.frame_index = storage_->frame_index_, .elapsed_ns = storage_->elapsed_ns_}, false);
  if (!completed) {
    log_error("application", completed.error().code, completed.error().name, "完成分发系统失败");
    return completed.error().code;
  }
  return error::success;
}

error application::advance_frame() try {
  storage_->is_running_ = true;
  const auto status = [&]() -> error {
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
        storage_->should_exit_ = true;
        return error::success;
      }
    }

    const auto dispatched = dispatch_completion_systems();
    if (dispatched.failed())
      return dispatched;
    const auto advanced = advance_resource_io();
    if (advanced == error::not_ready && !storage_->files_initialization_) {
      // 启动尚未完成，仍允许下一轮窗口事件与文件完成分发，不执行业务更新。
      return error::success;
    }
    if (advanced.failed()) {
      storage_->is_running_ = false;
      return advanced;
    }
    const auto current_time_ns = now_ns();
    const auto raw_delta = current_time_ns >= storage_->previous_time_ns_
                               ? current_time_ns - storage_->previous_time_ns_
                               : 0U;
    storage_->previous_time_ns_ = current_time_ns;
    storage_->elapsed_ns_ += raw_delta;
    const gneiss::frame_time time = {
        .frame_index = storage_->frame_index_,
        .delta_ns = raw_delta,
        .elapsed_ns = storage_->elapsed_ns_,
    };
    if (storage_->should_exit_)
      return error::success;
    const auto started = storage_->systems_.start(time, storage_->should_exit_);
    if (!started) {
      log_error("application", started.error().code, started.error().name, "宿主启动系统失败");
      return started.error().code;
    }
    if (storage_->should_exit_)
      return error::success;
    const auto transitioned = storage_->systems_.transition(time, storage_->should_exit_);
    if (!transitioned) {
      log_error("application", transitioned.error().code, transitioned.error().name,
                "业务状态转换失败");
      storage_->is_running_ = false;
      return transitioned.error().code;
    }
    if (storage_->should_exit_)
      return error::success;
    gneiss::measure(gneiss::loop_stage::draw_clear, [&] {});
    const auto run_systems = [&](app::stage phase) {
      auto result = storage_->systems_.run(phase, time, storage_->should_exit_);
      if (!result) {
        log_error("application", result.error().code, result.error().name, "应用系统执行失败");
        storage_->is_running_ = false;
        return result.error().code;
      }
      return error::success;
    };
    auto system_status = run_systems(app::stage::input);
    if (system_status.failed())
      return system_status;
    system_status = gneiss::measure(gneiss::loop_stage::update, [&] {
      GNEISS_PROFILE_SCOPE("application.update");
      return run_systems(app::stage::update);
    });
    if (system_status.failed())
      return system_status;
    system_status = run_systems(app::stage::synchronize);
    if (system_status.failed())
      return system_status;
    system_status = gneiss::measure(gneiss::loop_stage::render,
                                    [&] { return run_systems(app::stage::present); });
    if (system_status.failed())
      return system_status;
    ++storage_->frame_index_;
    return error::success;
  }();
  storage_->is_running_ = false;
  return status;
} catch (...) {
  storage_->is_running_ = false;
  throw;
}

result<app::lifecycle> application::step() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->stopping_host_ ||
      storage_->shutting_down_ || storage_->step_active_)
    return error::invalid_state;
  struct step_guard {
    bool& active;
    ~step_guard() { active = false; }
  } guard{storage_->step_active_};
  storage_->step_active_ = true;
  if (storage_->lifecycle_ == app::lifecycle::stopped) {
    if (storage_->lifecycle_result_.failed())
      return storage_->lifecycle_result_;
    return app::lifecycle::stopped;
  }
  if (!storage_->platform_initialized_ && !storage_->shutdown_requested_)
    return error::invalid_state;
  if (!storage_->step_started_) {
    storage_->systems_.freeze();
    storage_->previous_time_ns_ = now_ns();
    storage_->step_started_ = true;
  }
  if (storage_->should_exit_ || storage_->shutdown_requested_)
    storage_->lifecycle_ = app::lifecycle::stopping;
  if (storage_->lifecycle_ != app::lifecycle::stopping) {
    GNEISS_PROFILE_FRAME();
    GNEISS_PROFILE_SCOPE("application.loop");
    const auto status = advance_frame();
    if (status.failed()) {
      storage_->lifecycle_result_ = status;
      storage_->should_exit_ = true;
    }
    if (!storage_->should_exit_) {
      storage_->lifecycle_ =
          storage_->files_initialization_ ? app::lifecycle::running : app::lifecycle::starting;
      return storage_->lifecycle_;
    }
    storage_->lifecycle_ = app::lifecycle::stopping;
  }
  error status = error::success;
  if (!storage_->shutdown_requested_) {
    status = request_shutdown();
    if (status.ok())
      return app::lifecycle::stopping;
  } else {
    status = advance_shutdown();
  }
  if (status == error::not_ready)
    return app::lifecycle::stopping;
  if (status.failed() && !storage_->shutdown_complete_) {
    // 启动关闭请求失败仍需释放已建立的服务；保留最先出现的业务错误。
    (void)shutdown();
  }
  if (storage_->lifecycle_result_.ok())
    storage_->lifecycle_result_ = status;
  storage_->lifecycle_ = app::lifecycle::stopped;
  if (storage_->lifecycle_result_.failed())
    return storage_->lifecycle_result_;
  return app::lifecycle::stopped;
}

result<void> application::run() {
  GNEISS_PROFILE_THREAD("Gneiss main");
  loop_trace_file trace;
  const auto origin = timing_clock::now();
  trace.set_origin(origin);
  for (;;) {
#ifdef GNEISS_NATIVE_LOOP_WAIT
    auto* progress = static_cast<main_loop_wakeup*>(storage_->loop_wakeup_.get());
    const auto observed = progress ? progress->snapshot() : 0U;
#endif
    const loop_frame frame_trace(trace.get(), storage_->frame_index_, origin);
    auto status = step();
    if (!status)
      return status.error();
    if (*status == app::lifecycle::stopped)
      return {};
#ifdef GNEISS_NATIVE_LOOP_WAIT
    if (progress) {
      measure(loop_stage::idle_wait, [&] {
        GNEISS_PROFILE_SCOPE("application.idle_wait");
        (void)progress->wait(observed, std::chrono::milliseconds(4));
      });
      continue;
    }
#endif
    std::this_thread::yield();
  }
}

bool application::is_owner_thread() const noexcept {
  return storage_->owner_thread_ == std::this_thread::get_id();
}

gneiss::error application::request_shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_ ||
      storage_->stopping_host_)
    return gneiss::error::invalid_state;
  if (storage_->shutdown_requested_)
    return gneiss::error::success;
  storage_->shutdown_requested_ = true;
  storage_->accepts_requests_.store(false, std::memory_order_release);
  if (!stop_host())
    return error::success;
  return begin_service_shutdown();
}

error application::begin_service_shutdown() {
  if (storage_->file_close_)
    return error::success;
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
  if (storage_->asset_manager_)
    storage_->asset_manager_->request_stop();
  return gneiss::error::success;
}

gneiss::error application::advance_shutdown() {
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_ ||
      storage_->stopping_host_ || !storage_->shutdown_requested_)
    return gneiss::error::invalid_state;
  if (storage_->shutdown_complete_)
    return storage_->shutdown_result_;
  const auto completed = dispatch_completion_systems();
  if (completed.failed() && storage_->host_stop_result_.ok())
    storage_->host_stop_result_ = completed;
  if (!storage_->file_close_) {
    if (storage_->files_) {
      auto dispatched = storage_->files_->dispatch_completions(64);
      if (dispatched.failed())
        return dispatched;
    }
    if (storage_->asset_manager_)
      storage_->asset_manager_->advance();
    if (!stop_host())
      return error::not_ready;
    const auto begun = begin_service_shutdown();
    if (begun.failed())
      return begun;
  }
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
  if (!is_owner_thread() || storage_->is_running_ || storage_->shutting_down_ ||
      storage_->stopping_host_) {
    return gneiss::error::invalid_state;
  }
  // 桌面显式关闭及析构保底可等待；外部事件循环使用非阻塞 advance_shutdown。
  while (!stop_host()) {
    const auto completed = dispatch_completion_systems();
    if (completed.failed() && storage_->host_stop_result_.ok())
      storage_->host_stop_result_ = completed;
    if (storage_->files_)
      (void)storage_->files_->dispatch_completions(64);
    if (storage_->asset_manager_)
      storage_->asset_manager_->advance();
    std::this_thread::yield();
  }
  storage_->shutting_down_ = true;
  auto shutdown_result = storage_->host_stop_result_;
  if (storage_->asset_manager_)
    storage_->asset_manager_->stop();
  storage_->asset_manager_.reset();
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
    gneiss::render::device_stats stats;
    const auto render_shutdown_result = storage_->render_service_->shutdown(stats);
    if (shutdown_result == gneiss::error::success) {
      shutdown_result = render_shutdown_result;
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
  return gneiss::error::success;
}

bool application::accepts_requests() const noexcept {
  return storage_->accepts_requests_.load(std::memory_order_acquire);
}

gneiss::render::service& application::render() noexcept { return *storage_->render_service_; }

std::shared_ptr<gneiss::files::service> application::async_files() const noexcept {
  return storage_->files_;
}

gneiss::asset_manager& application::assets() noexcept { return *storage_->asset_manager_; }

gneiss::input::service& application::input() noexcept {
  GNEISS_ASSERT(is_owner_thread() && accepts_requests(), "输入服务只在所属线程和有效期内借用");
  return storage_->input_;
}

void application::request_exit() noexcept { storage_->should_exit_ = true; }

} // namespace gneiss::runtime
