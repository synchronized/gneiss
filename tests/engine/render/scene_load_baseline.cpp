// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include "function/render/detail/frame_packet.hpp"
#include <gneiss/engine/function/render/service.hpp>

#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
using clock_type = std::chrono::steady_clock;
double milliseconds(clock_type::time_point start) {
  return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}
// 仅诊断模式计时；墙钟耗时包含线程被系统暂停的时间，不等同于 CPU 执行时间。
struct measured_scope {
  double* output{};
  clock_type::time_point start = output != nullptr ? clock_type::now() : clock_type::time_point{};
  ~measured_scope() {
    if (output != nullptr) {
      *output += milliseconds(start);
    }
  }
};
struct loop_sample {
  double start_ms{};
  unsigned phase{};
  double callback_ms{};
  double scheduler_ms{};
  double scene_work_ms{};
  double sleep_ms{};
  // 直到下一次回调入口才能闭合，最后一行保持 -1。
  double outside_callback_ms{-1.0};
};
std::uint64_t peak_resident_bytes() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0
             ? static_cast<std::uint64_t>(counters.PeakWorkingSetSize)
             : 0U;
#else
  rusage usage{};
  return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U
                                             : 0U;
#endif
}
// 场景切换的预载也走设备变体选择和 CPU 准备，不能用同步预载阻断被测异步路径。
gneiss::error preload_scene(const std::shared_ptr<gneiss::runtime::application>& app,
                            gneiss::tasks::task_scheduler& scheduler, bool cooperative,
                            std::string_view uri, gneiss::scene_instance& scene) {
  auto request = app->assets().load<gneiss::scene>(uri);
  if (!request)
    return request.error();
  gneiss::asset_batch_request visuals;
  const auto deadline = clock_type::now() + std::chrono::minutes(15);
  while (clock_type::now() < deadline) {
    if (cooperative)
      (void)scheduler.run_ready();
    auto status = app->run(1);
    if (status.failed())
      return status;
    if (*request && request->status() != gneiss::error::not_ready) {
      auto content = request->snapshot();
      auto loaded = request->take();
      if (!loaded)
        return loaded.error().error;
      if (!content)
        return gneiss::error::invalid_state;
      auto built = gneiss::test_support::activate_scene(*app, *content);
      if (!built)
        return built.error();
      scene = built->instance.instance;
      visuals = std::move(built->visuals);
    }
    if (!*request && (!visuals || visuals.ready())) {
      if (!visuals)
        return gneiss::error::success;
      auto completed = visuals.take();
      return completed ? completed->result : completed.error();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return gneiss::error::not_ready;
}
} // namespace

// 显式运行的测量工具，不加入普通 CTest，缺少外部夹具不得当作通过。
int main(int argc, char** argv) try {
  if (argc < 3 || argc > 6) {
    std::fprintf(stderr, "用法：scene_load_baseline <资产目录> <输出前缀> [thread|cooperative] "
                         "[initial|switch|repeat|interact|cancel-prepare|"
                         "cancel-ready|failure|close] [trace]\n");
    return 2;
  }
  const bool trace_loop = argc == 6 && std::string_view(argv[5]) == "trace";
  if (argc == 6 && !trace_loop) {
    return 2;
  }
  const auto root = std::filesystem::absolute(argv[1]).string();
  const std::filesystem::path prefix(argv[2]);
  const std::string_view mode = argc >= 4 ? argv[3] : "thread";
  if (mode != "thread" && mode != "cooperative") {
    return 2;
  }
  const std::string_view scenario = argc >= 5 ? argv[4] : "initial";
  if (scenario != "initial" && scenario != "switch" && scenario != "repeat" &&
      scenario != "interact" && scenario != "cancel-prepare" && scenario != "cancel-ready" &&
      scenario != "failure" && scenario != "close") {
    return 2;
  }
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 1U,
       .mode = mode == "cooperative" ? gneiss::tasks::execution_mode::cooperative
                                     : gneiss::tasks::execution_mode::thread_pool});
  std::function<gneiss::error(gneiss::runtime::application*)> update;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&](const auto&) {
    return update ? update(app.get()) : gneiss::error::success;
  };
  desc.enable_window = true;
  desc.asset_root = root;
  constexpr std::string_view title = "Gneiss Scene Loading Baseline";
  desc.window.title = title;
  // 只有窗口交互验收显示窗口，其余 GPU 测量保持隐藏，避免打断桌面操作。
  desc.window.visible = false;
  desc.window.high_dpi = false;
  if (scenario == "interact") {
    desc.window.visible = true;
  }
  if (gneiss::error{gneiss::test_support::assign_value(
          gneiss::runtime::application::create(desc), app)} != gneiss::error::success ||
      gneiss::error{app->run(3U)} != gneiss::error::success) {
    return 3;
  }
  if (app->attach_task_executor(scheduler) != gneiss::error::success) {
    return 3;
  }
  gneiss::scene_instance scene{};
  constexpr std::string_view uri = "asset://scenes/scene.scene.json";
  gneiss::scene_instance previous_scene{};
  gneiss::game_object camera;
  gneiss::math::transform camera_transform;
  std::uint64_t previous_resources{};
  gneiss::render::frame_image previous_image;
  const bool preserve = scenario == "failure" || scenario.starts_with("cancel-");
  if (scenario != "initial") {
    const auto preload = preload_scene(app, scheduler, mode == "cooperative", uri, previous_scene);
    if (preload != gneiss::error::success) {
      std::fprintf(stderr, "异步预载失败：结果=%d\n", preload.code());
      return 11;
    }
    if (previous_scene == gneiss::scene_instance{} ||
        app->native_world() == nullptr ||
        gneiss::error{app->run(10U)} != gneiss::error::success) {
      return 11;
    }
    for (auto object : previous_scene.objects()) {
      if (object.try_component<gneiss::camera_component>()) {
        camera = object;
        camera_transform = object.local_transform();
        break;
      }
    }
    previous_resources = app->render().live_resource_count();
    if (preserve &&
        app->capture_frame(1024U, 768U, previous_image) != gneiss::error::success) {
      return 11;
    }
  }
  struct temporary_source {
    std::filesystem::path path;
    ~temporary_source() {
      if (!path.empty()) {
        std::error_code error;
        std::filesystem::remove(path, error);
      }
    }
  } temporary;
  std::string source_uri(uri);
  if (scenario == "failure") {
    const auto path = std::filesystem::path(root) / "gneiss-benchmark-invalid.scene.json";
    if (std::filesystem::exists(path)) {
      return 12;
    }
    // 结构文件损坏才是打开失败；缺失材质属于视觉依赖降级。
    temporary.path = path;
    std::ofstream output(path);
    output << "broken scene json";
    if (!output) {
      return 12;
    }
    source_uri = "asset://gneiss-benchmark-invalid.scene.json";
  }
  unsigned resized{};
  unsigned completed_switches{};
  double maximum_activation_ms{};
  bool minimized{}, restored{}, cancel_requested{}, closed{};
  double minimized_at_ms{};
  double cancel_ms{}, shutdown_ms{};
  bool request_released{};
  auto cancel_start = clock_type::now();
  std::size_t live_resources{};
  std::uint64_t sampled_frame{};
  struct render_sample {
    double elapsed;
    std::uint64_t frame;
    double interval;
    bool minimized;
  };
  std::vector<render_sample> render_samples;
  const auto start = clock_type::now();
  auto result = gneiss::error::success;
  std::string diagnostic;
  double activation_ms{};
  std::vector<loop_sample> loop_samples;
  if (trace_loop) {
    loop_samples.reserve(65536U);
  }
  std::vector<double> event_intervals;
  std::vector<double> minimized_event_intervals;
  // 基准自身的可观察阶段，不复用已删除的候选协议。
  enum class load_stage : unsigned { loading, visuals, completed };
  std::array<double, 3> phase_maximum_intervals{};
  auto previous_phase = load_stage::loading;
  double activation_elapsed_ms{};
  {
    gneiss::asset_request<gneiss::scene> request;
    gneiss::asset_batch_request visuals;
    result = gneiss::test_support::take_value(app->assets().load<gneiss::scene>(source_uri),
                                              request);
    if (result.ok() && scenario == "cancel-prepare") {
      cancel_start = clock_type::now();
      request.cancel();
      cancel_ms = milliseconds(cancel_start);
      cancel_requested = true;
    }
    auto previous = clock_type::now();
    auto deadline = previous + std::chrono::minutes(15);
    update = [&](gneiss::runtime::application* handle) {
      loop_sample* sample = nullptr;
      if (trace_loop) {
        const auto entered_ms = milliseconds(start);
        if (!loop_samples.empty()) {
          auto& prior = loop_samples.back();
          prior.outside_callback_ms = entered_ms - prior.start_ms - prior.callback_ms;
        }
        loop_samples.push_back(
            {.start_ms = entered_ms, .phase = static_cast<unsigned>(previous_phase)});
        sample = &loop_samples.back();
      }
      const measured_scope callback_time{.output =
                                             sample != nullptr ? &sample->callback_ms : nullptr};
      const bool currently_minimized = minimized && !restored;
      if (previous_phase != load_stage::completed) {
        const auto interval = milliseconds(previous);
        (currently_minimized ? minimized_event_intervals : event_intervals).push_back(interval);
        auto& phase_maximum = phase_maximum_intervals[static_cast<std::size_t>(previous_phase)];
        phase_maximum = std::max(phase_maximum, interval);
      }
      previous = clock_type::now();
      const auto render_stats = handle->render_statistics();
      if (sampled_frame != render_stats.presented_frames) {
        render_samples.push_back({milliseconds(start), render_stats.presented_frames,
                                  render_stats.latest_frame_interval_ms, currently_minimized});
        sampled_frame = render_stats.presented_frames;
      }
      const auto elapsed = milliseconds(start);
      if (scenario == "interact" && scene == gneiss::scene_instance{}) {
        if (camera.is_valid()) {
          auto transform = camera_transform;
          transform.translation[0] += static_cast<float>(std::sin(elapsed / 1000.0) * 0.1);
          if (camera.set_local_transform(transform).failed()) {
            return gneiss::error::internal;
          }
        }
#ifdef _WIN32
        const auto window = FindWindowA(nullptr, title.data());
        if (window && elapsed > 200.0 && resized < 10U &&
            resized < static_cast<unsigned>((elapsed - 200.0) / 100.0)) {
          ++resized;
          SetWindowPos(window, nullptr, 0, 0, 800 + static_cast<int>(resized % 2U) * 100, 600,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (window && elapsed > 500.0 && !minimized) {
          ShowWindow(window, SW_MINIMIZE);
          minimized = true;
          minimized_at_ms = elapsed;
        }
        if (window && minimized && elapsed - minimized_at_ms > 250.0 && !restored) {
          ShowWindow(window, SW_RESTORE);
          restored = true;
        }
#endif
      }
      if (scenario == "close" && elapsed > 100.0) {
        closed = true;
#ifdef _WIN32
        const auto window = FindWindowA(nullptr, title.data());
        if (window) {
          PostMessageA(window, WM_CLOSE, 0U, 0);
          return gneiss::error::success;
        }
#endif
        handle->request_exit();
        return gneiss::error::success;
      }
      if (clock_type::now() > deadline) {
        result = gneiss::error::not_ready;
        handle->request_exit();
        return gneiss::error::success;
      }
      if (mode == "cooperative") {
        const measured_scope scheduler_time{.output = sample != nullptr ? &sample->scheduler_ms
                                                                        : nullptr};
        (void)scheduler.run_ready();
      }
      {
        const measured_scope work_time{.output = sample ? &sample->scene_work_ms : nullptr};
        if (cancel_requested) {
          scene = previous_scene;
          activation_elapsed_ms = milliseconds(start);
          handle->request_exit();
          return gneiss::error::success;
        }
        if (request && request.status() != gneiss::error::not_ready) {
          if (scenario == "cancel-ready" && request.status().ok()) {
            cancel_start = clock_type::now();
            request.cancel();
            cancel_ms = milliseconds(cancel_start);
            cancel_requested = true;
            scene = previous_scene;
            activation_elapsed_ms = milliseconds(start);
            handle->request_exit();
            return gneiss::error::success;
          }
          auto content = request.snapshot();
          auto loaded = request.take();
          if (!loaded) {
            diagnostic = loaded.error().message;
            result = loaded.error().error;
            request.cancel();
            activation_elapsed_ms = milliseconds(start);
            if (scenario == "failure") {
              result = gneiss::error::success;
              scene = previous_scene;
            }
            handle->request_exit();
            return gneiss::error::success;
          }
          if (!content)
            return gneiss::error::invalid_state;
          const auto activation_start = clock_type::now();
          auto built = gneiss::test_support::activate_scene(*handle, *content);
          if (!built)
            return built.error();
          if (previous_scene.id()) {
            previous_scene.unload();
          }
          activation_ms = milliseconds(activation_start);
          maximum_activation_ms = std::max(maximum_activation_ms, activation_ms);
          scene = built->instance.instance;
          previous_scene = scene;
          visuals = std::move(built->visuals);
          ++completed_switches;
          activation_elapsed_ms = milliseconds(start);
          previous_phase = load_stage::visuals;
        }
        if (scene.id()) {
          if (visuals && !visuals.ready())
            return gneiss::error::success;
          if (visuals) {
            auto completed = visuals.take();
            if (!completed)
              return completed.error();
            if (completed->result.failed())
              return completed->result;
          }
          live_resources = handle->render().live_resource_count();
          if (scenario == "repeat") {
            if (live_resources != previous_resources)
              return gneiss::error::invalid_state;
            if (completed_switches < 3U) {
              result = gneiss::test_support::take_value(
                  handle->assets().load<gneiss::scene>(source_uri), request);
              scene = {};
              previous_phase = load_stage::loading;
              deadline = clock_type::now() + std::chrono::minutes(15);
              return result;
            }
          }
          previous_phase = load_stage::completed;
          handle->request_exit();
          return gneiss::error::success;
        }
      }
      {
        const measured_scope sleep_time{.output = sample != nullptr ? &sample->sleep_ms : nullptr};
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return gneiss::error::success;
    };
    if (result == gneiss::error::success) {
      const auto run_result =
          static_cast<gneiss::error>(gneiss::error{app->run(0U)});
      if (run_result != gneiss::error::success) {
        result = run_result;
      }
      if (!closed && scene == gneiss::scene_instance{} && result == gneiss::error::success) {
        result = gneiss::error::invalid_state;
      }
    }
    update = {};
    request_released = !request;
  }
  const auto load_and_drain_ms = milliseconds(start);
  const auto load_ms = activation_elapsed_ms;

  if (trace_loop) {
    std::ofstream trace(prefix.string() + ".loop.csv");
    trace << "start_ms,phase,callback_ms,scheduler_ms,scene_work_ms,sleep_ms,outside_callback_ms\n";
    trace << std::setprecision(10);
    for (const auto& sample : loop_samples) {
      trace << sample.start_ms << ',' << sample.phase << ',' << sample.callback_ms << ','
            << sample.scheduler_ms << ',' << sample.scene_work_ms << ',' << sample.sleep_ms << ','
            << sample.outside_callback_ms << '\n';
    }
    if (!trace.good()) {
      return 9;
    }
  }
  if (closed) {
    const auto closing_start = clock_type::now();
    app.reset();
    shutdown_ms = milliseconds(closing_start);
    std::ofstream report(prefix.string() + ".json");
    report << "{\"closed\":true,\"shutdown_ms\":" << shutdown_ms
           << ",\"retained_tasks\":" << scheduler.stats().retained << "}\n";
    return report.good() && scheduler.stats().retained == 0U ? 0 : 13;
  }
  std::printf("scene_load result=%d milliseconds=%.3f peak_resident_bytes=%llu\n", result.code(),
              load_ms, static_cast<unsigned long long>(peak_resident_bytes()));
  if (!diagnostic.empty()) {
    std::printf("scene_load diagnostic=%s\n", diagnostic.c_str());
  }
  std::fflush(stdout);
  if (result != gneiss::error::success) {
    live_resources = app->render().live_resource_count();
    const auto peak = peak_resident_bytes();
    app.reset();
    std::ofstream report(prefix.string() + ".json");
    report << "{\"result\":" << result.code() << ",\"load_ms\":" << load_ms
           << ",\"live_resources\":" << live_resources << ",\"peak_resident_bytes\":" << peak
           << ",\"retained_tasks\":" << scheduler.stats().retained << "}\n";
    return 4;
  }
  const auto nodes = scene.size();
  if (nodes == 0U)
    return 5;
  const auto rendering_start = clock_type::now();
  if (gneiss::error{app->run(60U)} != gneiss::error::success) {
    return 6;
  }
  const auto render_ms = milliseconds(rendering_start);
  const auto stats = app->render_statistics();
  const auto capture_start = clock_type::now();
  gneiss::render::frame_image image;
  if (app->capture_frame(1024U, 768U, image) != gneiss::error::success) {
    return 8;
  }
  const auto capture_ms = milliseconds(capture_start);
  live_resources = app->render().live_resource_count();
  if (preserve && (image.pixels != previous_image.pixels || live_resources != previous_resources)) {
    return 14;
  }
  double retry_ms{};
  bool retried{};
  if (scenario == "failure") {
    const auto retry_start = clock_type::now();
    if (preload_scene(app, scheduler, mode == "cooperative", uri, scene) !=
        gneiss::error::success)
      return 16;
    previous_scene.unload();
    if (gneiss::error{app->run(60U)} != gneiss::error::success)
      return 16;
    retry_ms = milliseconds(retry_start);
    live_resources = app->render().live_resource_count();
    gneiss::render::frame_image retried_image;
    if (app->capture_frame(1024U, 768U, retried_image) != gneiss::error::success ||
        retried_image.pixels != previous_image.pixels || live_resources != previous_resources) {
      return 17;
    }
    retried = true;
  }
  std::ofstream frames(prefix.string() + ".csv");
  frames << "elapsed_ms,presented_frame,latest_interval_ms,minimized\n";
  for (const auto& sample : render_samples) {
    frames << sample.elapsed << ',' << sample.frame << ',' << sample.interval << ','
           << sample.minimized << '\n';
  }
  std::ofstream pixels(prefix.string() + ".ppm", std::ios::binary);
  pixels << "P6\n" << image.width << ' ' << image.height << "\n255\n";
  for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U) {
    pixels.write(reinterpret_cast<const char*>(image.pixels.data() + offset), 3);
  }
  std::ofstream report(prefix.string() + ".json");
  std::ranges::sort(event_intervals);
  const auto event_p95 =
      event_intervals.empty() ? 0.0 : event_intervals[(event_intervals.size() - 1U) * 95U / 100U];
  const auto event_max = event_intervals.empty() ? 0.0 : event_intervals.back();
  const auto event_percentile = [&](std::size_t percentile) {
    return event_intervals.empty()
               ? 0.0
               : event_intervals[(event_intervals.size() - 1U) * percentile / 100U];
  };
  report << std::setprecision(10)
         << "{\n  \"loop_trace_enabled\": " << (trace_loop ? "true" : "false")
         << ",\n  \"event_interval_count\": " << event_intervals.size()
         << ",\n  \"event_interval_p50_ms\": " << event_percentile(50U)
         << ",\n  \"event_interval_p99_ms\": " << event_percentile(99U)
         << ",\n  \"event_interval_exceedances\": [";
  bool first_threshold = true;
  for (const auto threshold : {16.7, 33.3, 50.0, 100.0}) {
    const auto count = std::ranges::count_if(
        event_intervals, [threshold](double interval) { return interval > threshold; });
    if (!first_threshold) {
      report << ',';
    }
    first_threshold = false;
    report << "{\"threshold_ms\":" << threshold << ",\"count\":" << count << ",\"ratio\":"
           << (event_intervals.empty()
                   ? 0.0
                   : static_cast<double>(count) / static_cast<double>(event_intervals.size()))
           << '}';
  }
  report << "],\n";
  report << std::setprecision(10) << "  \"load_ms\": " << load_ms << ",\n  \"mode\": \"" << mode
         << "\""
         << ",\n  \"scenario\": \"" << scenario << "\""
         << ",\n  \"load_and_drain_ms\": " << load_and_drain_ms
         << ",\n  \"verified_capture_ms\": " << load_and_drain_ms + render_ms + capture_ms
         << ",\n  \"completed_switches\": " << completed_switches
         << ",\n  \"maximum_activation_ms\": " << maximum_activation_ms
         << ",\n  \"live_resources\": " << live_resources
         << ",\n  \"old_resources\": " << previous_resources << ",\n  \"cancel_ms\": " << cancel_ms
         << ",\n  \"cancel_requested\": " << (cancel_requested ? "true" : "false")
         << ",\n  \"request_released\": " << (request_released ? "true" : "false")
         << ",\n  \"retried\": " << (retried ? "true" : "false")
         << ",\n  \"retry_ms\": " << retry_ms << ",\n  \"resizes\": " << resized
         << ",\n  \"restored\": " << restored << ",\n  \"minimized\": " << minimized
         << ",\n  \"minimized_at_ms\": " << minimized_at_ms
         << ",\n  \"activation_ms\": " << activation_ms
         << ",\n  \"event_interval_p95_ms\": " << event_p95
         << ",\n  \"event_interval_max_ms\": " << event_max
         << ",\n  \"minimized_event_count\": " << minimized_event_intervals.size()
         << ",\n  \"minimized_event_max_ms\": "
         << (minimized_event_intervals.empty()
                 ? 0.0
                 : *std::ranges::max_element(minimized_event_intervals))
         << ",\n  \"phase_maximum_event_interval_ms\": {";
  constexpr std::array phase_names{"loading", "visuals", "completed"};
  for (std::size_t index = 0U; index < phase_names.size(); ++index) {
    if (index != 0U) {
      report << ',';
    }
    report << '"' << phase_names[index] << "\":" << phase_maximum_intervals[index];
  }
  report << "}" << ",\n  \"render_60_ticks_ms\": " << render_ms
         << ",\n  \"capture_ms\": " << capture_ms << ",\n  \"nodes\": " << nodes
         << ",\n  \"presented_frames\": " << stats.presented_frames
         << ",\n  \"peak_resident_bytes\": " << peak_resident_bytes() << "\n}\n";
  return pixels.good() && report.good() ? 0 : 9;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 10;
}
