// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_asset_reload_internal.hpp"
#include "engine/function/application/application_scene_load_internal.hpp"

#include <gneiss/engine/application.hpp>
#include <gneiss/engine/scene.h>

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
// 场景切换的预载也走设备变体选择和有界上传，不能用同步预载阻断被测异步路径。
gneiss_result preload_scene(gneiss::application& app, gneiss::tasks::task_scheduler& scheduler,
                            bool cooperative, std::string_view uri, gneiss_scene_instance& scene) {
  using namespace gneiss::application_internal;
  std::uint64_t request{};
  auto result = request_scene_load(app.get(), uri, 1U, 1U, request);
  const auto deadline = clock_type::now() + std::chrono::minutes(15);
  while (result == GNEISS_SUCCESS && clock_type::now() < deadline) {
    if (cooperative) {
      (void)scheduler.run_ready();
    }
    result = static_cast<gneiss_result>(app.run(1U));
    if (result != GNEISS_SUCCESS) {
      break;
    }
    scene_load_completion completion;
    bool finished{};
    result = poll_scene_load(app.get(), completion, finished);
    if (finished) {
      return completion.result;
    }
    scene_load_progress progress;
    bool active{};
    if (result == GNEISS_SUCCESS) {
      result = query_scene_load_progress(app.get(), progress, active);
    }
    if (result == GNEISS_SUCCESS && active && progress.phase == scene_load_phase::ready) {
      result = activate_scene_load(app.get(), request, completion);
      if (result == GNEISS_SUCCESS) {
        scene = completion.scene;
        // 消费预载终态并回收旧域，之后才能发起被测切换。
        result = poll_scene_load(app.get(), completion, finished);
      }
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  (void)cancel_scene_load(app.get(), request);
  return result == GNEISS_SUCCESS ? GNEISS_ERROR_NOT_READY : result;
}
} // namespace

// 显式运行的测量工具，不加入普通 CTest，缺少外部夹具不得当作通过。
int main(int argc, char** argv) try {
  if (argc < 3 || argc > 6) {
    std::fprintf(stderr, "用法：scene_load_baseline <资产目录> <输出前缀> [thread|cooperative] "
                         "[initial|switch|repeat|interact|cancel-prepare|cancel-assets|cancel-gpu|"
                         "cancel-verify|cancel-ready|failure|close] [trace]\n");
    return 2;
  }
  const bool trace_loop = argc == 6 && std::string_view(argv[5]) == "trace";
  if (argc == 6 && !trace_loop) {
    return 2;
  }
  const auto root = std::filesystem::absolute(argv[1]).string();
  const std::filesystem::path prefix(argv[2]);
  const std::string_view mode = argc >= 4 ? argv[3] : "sync";
  if (mode != "sync" && mode != "thread" && mode != "cooperative") {
    return 2;
  }
  const std::string_view scenario = argc >= 5 ? argv[4] : "initial";
  if (scenario != "initial" && scenario != "switch" && scenario != "repeat" &&
      scenario != "interact" && scenario != "cancel-prepare" && scenario != "cancel-verify" &&
      scenario != "cancel-assets" && scenario != "cancel-gpu" && scenario != "cancel-ready" &&
      scenario != "failure" && scenario != "close") {
    return 2;
  }
  if (mode == "sync" && scenario != "initial") {
    return 2;
  }
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 1U,
       .mode = mode == "cooperative" ? gneiss::tasks::execution_mode::cooperative
                                     : gneiss::tasks::execution_mode::thread_pool});
  std::function<gneiss_result(gneiss_application)> update;
  gneiss::application app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &update;
  desc.update = [](gneiss_application handle, const gneiss_frame_time*, void* data) {
    auto& callback = *static_cast<std::function<gneiss_result(gneiss_application)>*>(data);
    return callback ? callback(handle) : GNEISS_SUCCESS;
  };
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  constexpr std::string_view title = "Gneiss Scene Loading Baseline";
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  // 只有窗口交互验收显示窗口，其余 GPU 测量保持隐藏，避免打断桌面操作。
  desc.window_flags = GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT;
  if (scenario == "interact") {
    desc.window_flags |= GNEISS_APPLICATION_WINDOW_VISIBLE_BIT;
  }
  if (gneiss::application::create_native(desc, app) != gneiss::result::success ||
      app.run(3U) != gneiss::result::success) {
    return 3;
  }
  if (mode != "sync" &&
      gneiss::application_internal::attach_task_executor(app.get(), scheduler) != GNEISS_SUCCESS) {
    return 3;
  }
  gneiss_scene_instance scene{};
  constexpr std::string_view uri = "asset://scenes/scene.scene.json";
  gneiss_scene_instance previous_scene{};
  gneiss_world previous_world{};
  gneiss_scene_instance_node_info camera = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  std::uint64_t previous_resources{};
  gneiss::render_internal::frame_image previous_image;
  const bool preserve = scenario == "failure" || scenario.starts_with("cancel-");
  if (scenario != "initial") {
    const auto preload = preload_scene(app, scheduler, mode == "cooperative", uri, previous_scene);
    if (preload != GNEISS_SUCCESS) {
      std::fprintf(stderr, "异步预载失败：结果=%d\n", preload);
      return 11;
    }
    if (previous_scene == GNEISS_NULL_SCENE_INSTANCE ||
        gneiss_application_get_world(app.get(), &previous_world) != GNEISS_SUCCESS ||
        app.run(10U) != gneiss::result::success) {
      return 11;
    }
    std::uint64_t count{};
    if (gneiss_scene_instance_get_node_count(app.get(), previous_scene, &count) != GNEISS_SUCCESS) {
      return 11;
    }
    for (std::uint64_t index = 0U; index < count; ++index) {
      gneiss_scene_instance_node_info node = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
      if (gneiss_scene_instance_get_node_info(app.get(), previous_scene, index, &node) !=
          GNEISS_SUCCESS) {
        return 11;
      }
      if ((node.component_flags & GNEISS_SCENE_NODE_COMPONENT_CAMERA) != 0U) {
        camera = node;
        break;
      }
    }
    gneiss::application_internal::scene_retirement_statistics measured;
    if (gneiss::application_internal::query_scene_retirement(app.get(), measured) !=
        GNEISS_SUCCESS) {
      return 11;
    }
    previous_resources = measured.live_resources;
    if (preserve && gneiss::application_internal::capture_frame(app.get(), 1024U, 768U,
                                                                previous_image) != GNEISS_SUCCESS) {
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
  gneiss::application_internal::scene_retirement_statistics retirement;
  std::uint64_t sampled_frame{};
  struct render_sample {
    double elapsed;
    std::uint64_t frame;
    double interval;
    bool minimized;
  };
  std::vector<render_sample> render_samples;
  const auto start = clock_type::now();
  auto result = GNEISS_SUCCESS;
  gneiss::application_internal::scene_load_completion completion;
  std::vector<loop_sample> loop_samples;
  if (trace_loop) {
    loop_samples.reserve(65536U);
  }
  std::vector<double> event_intervals;
  std::vector<double> minimized_event_intervals;
  std::array<double, 8> phase_maximum_intervals{};
  auto previous_phase = gneiss::application_internal::scene_load_phase::preparing;
  double activation_elapsed_ms{};
  if (mode == "sync") {
    result = gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene);
  } else {
    using namespace gneiss::application_internal;
    std::uint64_t request{};
    if (result == GNEISS_SUCCESS) {
      result = request_scene_load(app.get(), source_uri, 1U, 1U, request);
    }
    if (result == GNEISS_SUCCESS && scenario == "cancel-prepare") {
      cancel_start = clock_type::now();
      result = cancel_scene_load(app.get(), request);
      cancel_requested = result == GNEISS_SUCCESS;
    }
    auto previous = clock_type::now();
    auto deadline = previous + std::chrono::minutes(15);
    update = [&](gneiss_application handle) {
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
      if (scene == GNEISS_NULL_SCENE_INSTANCE) {
        const auto interval = milliseconds(previous);
        (currently_minimized ? minimized_event_intervals : event_intervals).push_back(interval);
        auto& phase_maximum = phase_maximum_intervals[static_cast<std::size_t>(previous_phase)];
        phase_maximum = std::max(phase_maximum, interval);
        scene_load_progress before_work;
        bool active{};
        if (query_scene_load_progress(handle, before_work, active) == GNEISS_SUCCESS && active) {
          if (previous_phase != before_work.phase) {
            std::fprintf(stderr, "scene_load phase=%u elapsed_ms=%.3f completed=%zu total=%zu\n",
                         static_cast<unsigned>(before_work.phase), milliseconds(start),
                         before_work.completed, before_work.total);
          }
          previous_phase = before_work.phase;
        }
      }
      previous = clock_type::now();
      gneiss::render_internal::render_queue_stats render_stats;
      if (query_render_statistics(handle, render_stats) != GNEISS_SUCCESS) {
        return GNEISS_ERROR_INTERNAL;
      }
      if (sampled_frame != render_stats.presented_frames) {
        render_samples.push_back({milliseconds(start), render_stats.presented_frames,
                                  render_stats.latest_frame_interval_ms, currently_minimized});
        sampled_frame = render_stats.presented_frames;
      }
      const auto elapsed = milliseconds(start);
      if (scenario == "interact" && scene == GNEISS_NULL_SCENE_INSTANCE) {
        if (camera.node != GNEISS_NULL_SCENE_NODE_ID) {
          auto transform = camera.local_transform;
          transform.translation[0] += static_cast<float>(std::sin(elapsed / 1000.0) * 0.1);
          if (gneiss_scene_node_set_local_transform(previous_world, camera.node, &transform) !=
              GNEISS_SUCCESS) {
            return GNEISS_ERROR_INTERNAL;
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
          return GNEISS_SUCCESS;
        }
#endif
        return gneiss_application_request_exit(handle);
      }
      if (clock_type::now() > deadline) {
        result = GNEISS_ERROR_NOT_READY;
        return gneiss_application_request_exit(handle);
      }
      if (mode == "cooperative") {
        const measured_scope scheduler_time{.output = sample != nullptr ? &sample->scheduler_ms
                                                                        : nullptr};
        (void)scheduler.run_ready();
      }
      bool finished{};
      if (result == GNEISS_SUCCESS) {
        const measured_scope poll_time{.output =
                                           sample != nullptr ? &sample->scene_poll_ms : nullptr};
        result = poll_scene_load(handle, completion, finished);
      }
      if (awaiting_cleanup && result == GNEISS_SUCCESS) {
        scene_load_progress cleanup;
        bool available{};
        result = query_scene_load_progress(handle, cleanup, available);
        if (finished) {
          return GNEISS_ERROR_INVALID_STATE; // 清理补报不得形成第二个终态。
        }
        if (available && cleanup.cleanup_complete) {
          completion.progress = cleanup;
          cancel_cleanup_ms = milliseconds(cancel_start);
          scene = previous_scene;
          return gneiss_application_request_exit(handle);
        }
        {
          const measured_scope sleep_time{.output =
                                              sample != nullptr ? &sample->sleep_ms : nullptr};
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return result;
      }
      if (finished || result != GNEISS_SUCCESS) {
        if (finished) {
          result = completion.result;
          if (result != GNEISS_SUCCESS)
            std::fprintf(stderr, "scene preparation: %s\n", completion.message.c_str());
          activation_elapsed_ms = milliseconds(start);
          if (preserve &&
              ((scenario == "failure" && completion.progress.phase == scene_load_phase::failed) ||
               (cancel_requested && completion.progress.phase == scene_load_phase::cancelled))) {
            result = GNEISS_SUCCESS;
            scene = previous_scene;
            if (cancel_requested) {
              cancel_ms = milliseconds(cancel_start);
              cancel_cleanup_ms = cancel_ms;
              if (completion.progress.cleanup_pending) {
                awaiting_cleanup = observed_cleanup_pending = true;
                scene = GNEISS_NULL_SCENE_INSTANCE;
                return GNEISS_SUCCESS;
              }
            }
          }
        }
        return gneiss_application_request_exit(handle);
      }
      if (scene != GNEISS_NULL_SCENE_INSTANCE) {
        (void)query_scene_retirement(handle, retirement);
        maximum_retirement_ms = std::max(maximum_retirement_ms, retirement.last_ms);
        if (scenario == "repeat") {
          if (retirement.live_resources != previous_resources || retirement.pending)
            return GNEISS_ERROR_INVALID_STATE;
          if (completed_switches < 3U) {
            result = request_scene_load(handle, source_uri, 1U, completed_switches + 1U, request);
            scene = GNEISS_NULL_SCENE_INSTANCE;
            // 超时属于单次请求，不能让后续切换消耗前两次加载已经占用的期限。
            deadline = clock_type::now() + std::chrono::minutes(15);
            return result;
          }
        }
        return gneiss_application_request_exit(handle);
      }
      scene_load_progress progress;
      bool active{};
      if (query_scene_load_progress(handle, progress, active) == GNEISS_SUCCESS && active) {
        const bool cancel_phase =
            scenario == "cancel-assets"
                ? progress.phase == scene_load_phase::assets && progress.completed > 0U
            : scenario == "cancel-verify" ? progress.phase == scene_load_phase::verifying
            : scenario == "cancel-gpu"
                ? progress.gpu_in_flight
                : scenario == "cancel-ready" && progress.phase == scene_load_phase::ready;
        if (cancel_phase && !cancel_requested) {
          cancel_start = clock_type::now();
          result = cancel_scene_load(handle, request);
          cancel_requested = result == GNEISS_SUCCESS;
        } else if (!cancel_requested && progress.phase == scene_load_phase::ready) {
          result = activate_scene_load(handle, request, completion);
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
      return GNEISS_SUCCESS;
    };
    if (result == GNEISS_SUCCESS) {
      const auto run_result = static_cast<gneiss_result>(app.run());
      if (run_result != GNEISS_SUCCESS) {
        result = run_result;
      }
      if (!closed && scene == GNEISS_NULL_SCENE_INSTANCE && result == GNEISS_SUCCESS) {
        result = GNEISS_ERROR_INVALID_STATE;
      }
    }
    update = {};
  }
  const auto load_and_drain_ms = milliseconds(start);
  const auto load_ms = mode == "sync" ? load_and_drain_ms : activation_elapsed_ms;

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
    app = gneiss::application{};
    shutdown_ms = milliseconds(closing_start);
    std::ofstream report(prefix.string() + ".json");
    report << "{\"closed\":true,\"shutdown_ms\":" << shutdown_ms
           << ",\"retained_tasks\":" << scheduler.stats().retained << "}\n";
    return report.good() && scheduler.stats().retained == 0U ? 0 : 13;
  }
  std::printf("scene_load result=%d milliseconds=%.3f peak_resident_bytes=%llu\n", result, load_ms,
              static_cast<unsigned long long>(peak_resident_bytes()));
  if (!completion.message.empty()) {
    std::printf("scene_load diagnostic=%s\n", completion.message.c_str());
  }
  std::fflush(stdout);
  if (result != GNEISS_SUCCESS) {
    (void)gneiss::application_internal::query_scene_retirement(app.get(), retirement);
    const auto peak = peak_resident_bytes();
    app = gneiss::application{};
    std::ofstream report(prefix.string() + ".json");
    report << "{\"result\":" << result << ",\"load_ms\":" << load_ms
           << ",\"candidate_resident_bytes\":" << completion.progress.resident_bytes
           << ",\"candidate_cpu_data_bytes\":" << completion.progress.cpu_data_bytes
           << ",\"candidate_texture_payload_bytes\":" << completion.progress.texture_payload_bytes
           << ",\"peak_upload_bytes\":" << completion.progress.peak_upload_bytes
           << ",\"application_logical_bytes\":" << completion.progress.application_logical_bytes
           << ",\"application_cpu_data_bytes\":" << completion.progress.application_cpu_data_bytes
           << ",\"available_bytes\":" << completion.progress.available_bytes
           << ",\"cleanup_ms\":" << completion.cleanup_ms
           << ",\"cleanup_complete\":" << (completion.progress.cleanup_complete ? "true" : "false")
           << ",\"upload_reserved_bytes\":" << completion.progress.upload_reserved_bytes
           << ",\"live_resources\":" << retirement.live_resources
           << ",\"peak_resident_bytes\":" << peak
           << ",\"retained_tasks\":" << scheduler.stats().retained << "}\n";
    return 4;
  }
  std::uint64_t nodes{};
  if (gneiss_scene_instance_get_node_count(app.get(), scene, &nodes) != GNEISS_SUCCESS ||
      nodes == 0U) {
    return 5;
  }
  const auto rendering_start = clock_type::now();
  if (app.run(60U) != gneiss::result::success) {
    return 6;
  }
  const auto render_ms = milliseconds(rendering_start);
  gneiss::render_internal::render_queue_stats stats;
  if (gneiss::application_internal::query_render_statistics(app.get(), stats) != GNEISS_SUCCESS) {
    return 7;
  }
  const auto capture_start = clock_type::now();
  gneiss::render_internal::frame_image image;
  if (gneiss::application_internal::capture_frame(app.get(), 1024U, 768U, image) !=
      GNEISS_SUCCESS) {
    return 8;
  }
  const auto capture_ms = milliseconds(capture_start);
  (void)gneiss::application_internal::query_scene_retirement(app.get(), retirement);
  if (preserve &&
      (image.pixels != previous_image.pixels || retirement.live_resources != previous_resources)) {
    return 14;
  }
  // 终态证明请求已清理，旧场景仍拥有其资源；失败后在同一 Application 内重新加载。
  if (preserve &&
      (!completion.progress.cleanup_complete || completion.progress.upload_reserved_bytes != 0U)) {
    return 15;
  }
  double retry_ms{};
  bool retried{};
  if (scenario == "failure") {
    const auto retry_start = clock_type::now();
    if (preload_scene(app, scheduler, mode == "cooperative", uri, scene) != GNEISS_SUCCESS ||
        app.run(60U) != gneiss::result::success) {
      return 16;
    }
    retry_ms = milliseconds(retry_start);
    gneiss::render_internal::frame_image retried_image;
    if (gneiss::application_internal::capture_frame(app.get(), 1024U, 768U, retried_image) !=
            GNEISS_SUCCESS ||
        retried_image.pixels != previous_image.pixels ||
        gneiss::application_internal::query_scene_retirement(app.get(), retirement) !=
            GNEISS_SUCCESS ||
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
         << ",\n  \"verify_ms\": " << completion.verify_ms
         << ",\n  \"verify_maximum_step_ms\": " << completion.verify_maximum_step_ms
         << ",\n  \"verify_maximum_open_ms\": " << completion.verify_maximum_open_ms
         << ",\n  \"verify_maximum_read_ms\": " << completion.verify_maximum_read_ms
         << ",\n  \"verify_maximum_hash_ms\": " << completion.verify_maximum_hash_ms
         << ",\n  \"upload_ms\": " << completion.upload_ms
         << ",\n  \"maximum_advance_ms\": " << completion.maximum_advance_ms
         << ",\n  \"activation_ms\": " << completion.activation_ms << ",\n  \"cleanup_ms\": "
         << (observed_cleanup_pending ? std::string{"null"} : std::to_string(completion.cleanup_ms))
         << ",\n  \"cleanup_complete\": "
         << (completion.progress.cleanup_complete ? "true" : "false")
         << ",\n  \"upload_reserved_bytes\": " << completion.progress.upload_reserved_bytes
         << ",\n  \"candidate_resident_bytes\": " << completion.progress.resident_bytes
         << ",\"candidate_cpu_data_bytes\":" << completion.progress.cpu_data_bytes
         << ",\"candidate_texture_payload_bytes\":" << completion.progress.texture_payload_bytes
         << ",\"peak_upload_bytes\":" << completion.progress.peak_upload_bytes
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
      "preparing", "assets",  "verifying", "instantiating",
      "ready",     "applied", "failed",    "cancelled",
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
