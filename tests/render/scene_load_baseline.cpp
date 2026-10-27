// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/render_frame_packet.hpp"
#include "runtime/application.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
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
  double scene_poll_ms{};
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
gneiss::engine::result preload_scene(const std::shared_ptr<gneiss::runtime::application>& app,
                                     gneiss::engine::task_scheduler& scheduler, bool cooperative,
                                     std::string_view uri, gneiss::scene_instance_id& scene) {
  using namespace gneiss::runtime;
  std::uint64_t request{};
  auto result = app->request_scene(uri, 1U, 1U, request);
  const auto deadline = clock_type::now() + std::chrono::minutes(15);
  while (result == gneiss::engine::result::success && clock_type::now() < deadline) {
    if (cooperative) {
      (void)scheduler.run_ready();
    }
    result = static_cast<gneiss::engine::result>(gneiss::engine::result{app->run(1U)});
    if (result != gneiss::engine::result::success) {
      break;
    }
    scene_load_completion completion;
    bool finished{};
    result = app->poll_scene(completion, finished);
    if (finished && completion.result != gneiss::engine::result::success) {
      return completion.result;
    }
    if (scene != gneiss::scene_instance_id{} && !app->visual_assets_loading())
      return gneiss::engine::result::success;
    scene_load_progress progress;
    bool active{};
    if (result == gneiss::engine::result::success) {
      active = app->scene_progress(progress);
    }
    if (result == gneiss::engine::result::success && active &&
        progress.phase == scene_load_phase::ready) {
      result = app->activate_scene(request, completion);
      if (result == gneiss::engine::result::success) {
        scene = completion.scene;
        // 消费预载终态并回收旧域，之后才能发起被测切换。
        result = app->poll_scene(completion, finished);
      }
      if (result != gneiss::engine::result::success)
        return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  (void)app->cancel_scene(request);
  return result == gneiss::engine::result::success ? gneiss::engine::result::not_ready : result;
}
} // namespace

// 显式运行的测量工具，不加入普通 CTest，缺少外部夹具不得当作通过。
int main(int argc, char** argv) try {
  if (argc < 3 || argc > 6) {
    std::fprintf(stderr, "用法：scene_load_baseline <资产目录> <输出前缀> [thread|cooperative] "
                         "[initial|switch|repeat|interact|cancel-prepare|cancel-assets|"
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
      scenario != "interact" && scenario != "cancel-prepare" && scenario != "cancel-assets" &&
      scenario != "cancel-ready" && scenario != "failure" && scenario != "close") {
    return 2;
  }
  gneiss::engine::task_scheduler scheduler(
      {.workers = 1U,
       .mode = mode == "cooperative" ? gneiss::engine::execution_mode::cooperative
                                     : gneiss::engine::execution_mode::thread_pool});
  std::function<gneiss::engine::result(gneiss::runtime::application*)> update;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&](const auto&) {
    return update ? update(app.get()) : gneiss::engine::result::success;
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
  if (gneiss::engine::result{gneiss::runtime::application::create(desc, app)} !=
          gneiss::engine::result::success ||
      gneiss::engine::result{app->run(3U)} != gneiss::engine::result::success) {
    return 3;
  }
  if (app->attach_task_executor(scheduler) != gneiss::engine::result::success) {
    return 3;
  }
  gneiss::scene_instance_id scene{};
  constexpr std::string_view uri = "asset://scenes/scene.scene.json";
  gneiss::scene_instance_id previous_scene{};
  gneiss::engine::world* previous_world{};
  gneiss::engine::scene_node_view camera;
  std::uint64_t previous_resources{};
  gneiss::engine::frame_image previous_image;
  const bool preserve = scenario == "failure" || scenario.starts_with("cancel-");
  if (scenario != "initial") {
    const auto preload = preload_scene(app, scheduler, mode == "cooperative", uri, previous_scene);
    if (preload != gneiss::engine::result::success) {
      std::fprintf(stderr, "异步预载失败：结果=%d\n", preload.code());
      return 11;
    }
    if (previous_scene == gneiss::scene_instance_id{} ||
        (previous_world = app->native_world()) == nullptr ||
        gneiss::engine::result{app->run(10U)} != gneiss::engine::result::success) {
      return 11;
    }
    std::uint64_t count{};
    if (gneiss::engine::result{app->scenes()->get_node_count(previous_scene, &count)} !=
        gneiss::engine::result::success) {
      return 11;
    }
    for (std::uint64_t index = 0U; index < count; ++index) {
      gneiss::engine::scene_node_view node;
      if (gneiss::engine::result{app->scenes()->get_node_info(previous_scene, index, node)} !=
          gneiss::engine::result::success) {
        return 11;
      }
      if (gneiss::has_flags(node.component_flags, gneiss::scene_node_components::camera)) {
        camera = node;
        break;
      }
    }
    const auto measured = app->scene_retirement();
    previous_resources = measured.live_resources;
    if (preserve &&
        app->capture_frame(1024U, 768U, previous_image) != gneiss::engine::result::success) {
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
    std::ifstream input(std::filesystem::path(root) / "scenes/scene.scene.json");
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto offset = text.find("asset://materials/");
    if (offset == std::string::npos) {
      return 12;
    }
    text.replace(offset, 18U, "asset://missing-materials/");
    temporary.path = path;
    std::ofstream output(path);
    output << text;
    if (!output) {
      return 12;
    }
    source_uri = "asset://gneiss-benchmark-invalid.scene.json";
  }
  unsigned resized{};
  unsigned completed_switches{};
  double maximum_activation_ms{}, maximum_retirement_ms{};
  bool minimized{}, restored{}, cancel_requested{}, closed{};
  bool awaiting_cleanup{}, observed_cleanup_pending{};
  double cancel_cleanup_ms{};
  double minimized_at_ms{};
  double cancel_ms{}, shutdown_ms{};
  auto cancel_start = clock_type::now();
  gneiss::runtime::scene_retirement_statistics retirement;
  std::uint64_t sampled_frame{};
  struct render_sample {
    double elapsed;
    std::uint64_t frame;
    double interval;
    bool minimized;
  };
  std::vector<render_sample> render_samples;
  const auto start = clock_type::now();
  auto result = gneiss::engine::result::success;
  gneiss::runtime::scene_load_completion completion;
  std::vector<loop_sample> loop_samples;
  if (trace_loop) {
    loop_samples.reserve(65536U);
  }
  std::vector<double> event_intervals;
  std::vector<double> minimized_event_intervals;
  std::array<double, static_cast<std::size_t>(gneiss::runtime::scene_load_phase::cancelled) + 1U>
      phase_maximum_intervals{};
  auto previous_phase = gneiss::runtime::scene_load_phase::preparing;
  double activation_elapsed_ms{};
  {
    using namespace gneiss::runtime;
    std::uint64_t request{};
    if (result == gneiss::engine::result::success) {
      result = app->request_scene(source_uri, 1U, 1U, request);
    }
    if (result == gneiss::engine::result::success && scenario == "cancel-prepare") {
      cancel_start = clock_type::now();
      result = app->cancel_scene(request);
      cancel_requested = result == gneiss::engine::result::success;
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
      if (scene == gneiss::scene_instance_id{}) {
        const auto interval = milliseconds(previous);
        (currently_minimized ? minimized_event_intervals : event_intervals).push_back(interval);
        auto& phase_maximum = phase_maximum_intervals[static_cast<std::size_t>(previous_phase)];
        phase_maximum = std::max(phase_maximum, interval);
        scene_load_progress before_work;
        if (handle->scene_progress(before_work)) {
          if (previous_phase != before_work.phase) {
            std::fprintf(stderr, "scene_load phase=%u elapsed_ms=%.3f completed=%zu total=%zu\n",
                         static_cast<unsigned>(before_work.phase), milliseconds(start),
                         before_work.completed, before_work.total);
          }
          previous_phase = before_work.phase;
        }
      }
      previous = clock_type::now();
      const auto render_stats = handle->render_statistics();
      if (sampled_frame != render_stats.presented_frames) {
        render_samples.push_back({milliseconds(start), render_stats.presented_frames,
                                  render_stats.latest_frame_interval_ms, currently_minimized});
        sampled_frame = render_stats.presented_frames;
      }
      const auto elapsed = milliseconds(start);
      if (scenario == "interact" && scene == gneiss::scene_instance_id{}) {
        if (camera.node.is_valid()) {
          auto transform = camera.local_transform;
          transform.translation[0] += static_cast<float>(std::sin(elapsed / 1000.0) * 0.1);
          if (gneiss::engine::result{previous_world->node_set_local_transform(
                  gneiss::scene_node_id{camera.node.get()}, transform)} !=
              gneiss::engine::result::success) {
            return gneiss::engine::result::internal;
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
          return gneiss::engine::result::success;
        }
#endif
        handle->request_exit();
        return gneiss::engine::result::success;
      }
      if (clock_type::now() > deadline) {
        result = gneiss::engine::result::not_ready;
        handle->request_exit();
        return gneiss::engine::result::success;
      }
      if (mode == "cooperative") {
        const measured_scope scheduler_time{.output = sample != nullptr ? &sample->scheduler_ms
                                                                        : nullptr};
        (void)scheduler.run_ready();
      }
      bool finished{};
      if (result == gneiss::engine::result::success) {
        const measured_scope poll_time{.output =
                                           sample != nullptr ? &sample->scene_poll_ms : nullptr};
        result = handle->poll_scene(completion, finished);
      }
      if (awaiting_cleanup && result == gneiss::engine::result::success) {
        scene_load_progress cleanup;
        bool available{};
        available = handle->scene_progress(cleanup);
        if (finished) {
          return gneiss::engine::result::invalid_state; // 清理补报不得形成第二个终态。
        }
        if (available && cleanup.cleanup_complete) {
          completion.progress = cleanup;
          cancel_cleanup_ms = milliseconds(cancel_start);
          scene = previous_scene;
          handle->request_exit();
          return gneiss::engine::result::success;
        }
        {
          const measured_scope sleep_time{.output =
                                              sample != nullptr ? &sample->sleep_ms : nullptr};
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return result;
      }
      if (finished || result != gneiss::engine::result::success) {
        if (finished) {
          result = completion.result;
          if (result != gneiss::engine::result::success)
            std::fprintf(stderr, "scene preparation: %s\n", completion.message.c_str());
          activation_elapsed_ms = milliseconds(start);
          if (preserve &&
              ((scenario == "failure" && completion.progress.phase == scene_load_phase::failed) ||
               (cancel_requested && completion.progress.phase == scene_load_phase::cancelled))) {
            result = gneiss::engine::result::success;
            scene = previous_scene;
            if (cancel_requested) {
              cancel_ms = milliseconds(cancel_start);
              cancel_cleanup_ms = cancel_ms;
              if (completion.progress.cleanup_pending) {
                awaiting_cleanup = observed_cleanup_pending = true;
                scene = gneiss::scene_instance_id{};
                return gneiss::engine::result::success;
              }
            }
          }
        }
        handle->request_exit();
        return gneiss::engine::result::success;
      }
      if (scene != gneiss::scene_instance_id{}) {
        if (handle->visual_assets_loading())
          return gneiss::engine::result::success;
        retirement = handle->scene_retirement();
        maximum_retirement_ms = std::max(maximum_retirement_ms, retirement.last_ms);
        if (scenario == "repeat") {
          if (retirement.live_resources != previous_resources || retirement.pending)
            return gneiss::engine::result::invalid_state;
          if (completed_switches < 3U) {
            result = handle->request_scene(source_uri, 1U, completed_switches + 1U, request);
            scene = gneiss::scene_instance_id{};
            // 超时属于单次请求，不能让后续切换消耗前两次加载已经占用的期限。
            deadline = clock_type::now() + std::chrono::minutes(15);
            return result;
          }
        }
        handle->request_exit();
        return gneiss::engine::result::success;
      }
      scene_load_progress progress;
      if (handle->scene_progress(progress)) {
        const bool cancel_phase =
            scenario == "cancel-assets"
                ? progress.phase == scene_load_phase::assets && progress.completed > 0U
                : scenario == "cancel-ready" && progress.phase == scene_load_phase::ready;
        if (cancel_phase && !cancel_requested) {
          cancel_start = clock_type::now();
          result = handle->cancel_scene(request);
          cancel_requested = result == gneiss::engine::result::success;
        } else if (!cancel_requested && progress.phase == scene_load_phase::ready) {
          result = handle->activate_scene(request, completion);
          scene = completion.scene;
          ++completed_switches;
          maximum_activation_ms = std::max(maximum_activation_ms, completion.activation_ms);
          activation_elapsed_ms = milliseconds(start);
        }
      }
      {
        const measured_scope sleep_time{.output = sample != nullptr ? &sample->sleep_ms : nullptr};
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return gneiss::engine::result::success;
    };
    if (result == gneiss::engine::result::success) {
      const auto run_result =
          static_cast<gneiss::engine::result>(gneiss::engine::result{app->run(0U)});
      if (run_result != gneiss::engine::result::success) {
        result = run_result;
      }
      if (!closed && scene == gneiss::scene_instance_id{} &&
          result == gneiss::engine::result::success) {
        result = gneiss::engine::result::invalid_state;
      }
    }
    update = {};
  }
  const auto load_and_drain_ms = milliseconds(start);
  const auto load_ms = activation_elapsed_ms;

  if (trace_loop) {
    std::ofstream trace(prefix.string() + ".loop.csv");
    trace << "start_ms,phase,callback_ms,scheduler_ms,scene_poll_ms,sleep_ms,outside_callback_ms\n";
    trace << std::setprecision(10);
    for (const auto& sample : loop_samples) {
      trace << sample.start_ms << ',' << sample.phase << ',' << sample.callback_ms << ','
            << sample.scheduler_ms << ',' << sample.scene_poll_ms << ',' << sample.sleep_ms << ','
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
  if (!completion.message.empty()) {
    std::printf("scene_load diagnostic=%s\n", completion.message.c_str());
  }
  std::fflush(stdout);
  if (result != gneiss::engine::result::success) {
    retirement = app->scene_retirement();
    const auto peak = peak_resident_bytes();
    app.reset();
    std::ofstream report(prefix.string() + ".json");
    report << "{\"result\":" << result.code() << ",\"load_ms\":" << load_ms
           << ",\"candidate_resident_bytes\":" << completion.progress.resident_bytes
           << ",\"candidate_cpu_data_bytes\":" << completion.progress.cpu_data_bytes
           << ",\"candidate_texture_payload_bytes\":" << completion.progress.texture_payload_bytes
           << ",\"application_logical_bytes\":" << completion.progress.application_logical_bytes
           << ",\"application_cpu_data_bytes\":" << completion.progress.application_cpu_data_bytes
           << ",\"available_bytes\":" << completion.progress.available_bytes
           << ",\"cleanup_ms\":" << completion.cleanup_ms
           << ",\"cleanup_complete\":" << (completion.progress.cleanup_complete ? "true" : "false")
           << ",\"live_resources\":" << retirement.live_resources
           << ",\"peak_resident_bytes\":" << peak
           << ",\"retained_tasks\":" << scheduler.stats().retained << "}\n";
    return 4;
  }
  std::uint64_t nodes{};
  if (gneiss::engine::result{app->scenes()->get_node_count(scene, &nodes)} !=
          gneiss::engine::result::success ||
      nodes == 0U) {
    return 5;
  }
  const auto rendering_start = clock_type::now();
  if (gneiss::engine::result{app->run(60U)} != gneiss::engine::result::success) {
    return 6;
  }
  const auto render_ms = milliseconds(rendering_start);
  const auto stats = app->render_statistics();
  const auto capture_start = clock_type::now();
  gneiss::engine::frame_image image;
  if (app->capture_frame(1024U, 768U, image) != gneiss::engine::result::success) {
    return 8;
  }
  const auto capture_ms = milliseconds(capture_start);
  retirement = app->scene_retirement();
  if (preserve &&
      (image.pixels != previous_image.pixels || retirement.live_resources != previous_resources)) {
    return 14;
  }
  // 终态证明请求已清理，旧场景仍拥有其资源；失败后在同一 Application 内重新加载。
  if (preserve && (!completion.progress.cleanup_complete)) {
    return 15;
  }
  double retry_ms{};
  bool retried{};
  if (scenario == "failure") {
    const auto retry_start = clock_type::now();
    if (preload_scene(app, scheduler, mode == "cooperative", uri, scene) !=
            gneiss::engine::result::success ||
        gneiss::engine::result{app->run(60U)} != gneiss::engine::result::success) {
      return 16;
    }
    retry_ms = milliseconds(retry_start);
    retirement = app->scene_retirement();
    gneiss::engine::frame_image retried_image;
    if (app->capture_frame(1024U, 768U, retried_image) != gneiss::engine::result::success ||
        retried_image.pixels != previous_image.pixels ||
        retirement.live_resources != previous_resources || retirement.pending) {
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
         << ",\n  \"maximum_retirement_ms\": " << maximum_retirement_ms
         << ",\n  \"retirement_ms\": " << retirement.last_ms
         << ",\n  \"live_resources\": " << retirement.live_resources
         << ",\n  \"old_resources\": " << previous_resources << ",\n  \"cancel_ms\": " << cancel_ms
         << ",\n  \"cancel_cleanup_ms\": " << cancel_cleanup_ms
         << ",\n  \"observed_cleanup_pending\": " << (observed_cleanup_pending ? "true" : "false")
         << ",\n  \"retried\": " << (retried ? "true" : "false")
         << ",\n  \"retry_ms\": " << retry_ms << ",\n  \"resizes\": " << resized
         << ",\n  \"restored\": " << restored << ",\n  \"minimized\": " << minimized
         << ",\n  \"minimized_at_ms\": " << minimized_at_ms
         << ",\n  \"prepare_ms\": " << completion.prepare_ms
         << ",\n  \"asset_prepare_ms\": " << completion.asset_prepare_ms
         << ",\n  \"maximum_advance_ms\": " << completion.maximum_advance_ms
         << ",\n  \"activation_ms\": " << completion.activation_ms << ",\n  \"cleanup_ms\": "
         << (observed_cleanup_pending ? std::string{"null"} : std::to_string(completion.cleanup_ms))
         << ",\n  \"cleanup_complete\": "
         << (completion.progress.cleanup_complete ? "true" : "false")
         << ",\n  \"candidate_resident_bytes\": " << completion.progress.resident_bytes
         << ",\"candidate_cpu_data_bytes\":" << completion.progress.cpu_data_bytes
         << ",\"candidate_texture_payload_bytes\":" << completion.progress.texture_payload_bytes
         << ",\"application_logical_bytes\":" << completion.progress.application_logical_bytes
         << ",\"application_cpu_data_bytes\":" << completion.progress.application_cpu_data_bytes
         << ",\"available_bytes\":" << completion.progress.available_bytes
         << ",\n  \"event_interval_p95_ms\": " << event_p95
         << ",\n  \"event_interval_max_ms\": " << event_max
         << ",\n  \"minimized_event_count\": " << minimized_event_intervals.size()
         << ",\n  \"minimized_event_max_ms\": "
         << (minimized_event_intervals.empty()
                 ? 0.0
                 : *std::ranges::max_element(minimized_event_intervals))
         << ",\n  \"phase_maximum_event_interval_ms\": {";
  constexpr std::array phase_names{
      "preparing", "assets", "instantiating", "ready", "applied", "failed", "cancelled",
  };
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
