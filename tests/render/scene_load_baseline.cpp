// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_asset_reload_internal.h"
#include "application/application_scene_load_internal.h"

#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include <algorithm>
#include <chrono>
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
} // namespace

// 显式运行的测量工具，不加入普通 CTest，缺少外部夹具不得当作通过。
int main(int argc, char** argv) try {
  if (argc != 3 && argc != 4) {
    std::fprintf(stderr, "用法：scene_load_baseline <资产目录> <输出前缀> [thread|cooperative]\n");
    return 2;
  }
  const auto root = std::filesystem::absolute(argv[1]).string();
  const std::filesystem::path prefix(argv[2]);
  const std::string_view mode = argc == 4 ? argv[3] : "sync";
  if (mode != "sync" && mode != "thread" && mode != "cooperative") {
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
  desc.window_flags =
      GNEISS_APPLICATION_WINDOW_VISIBLE_BIT | GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT;
  if (gneiss::application::create(desc, app) != gneiss::result::success ||
      app.run(3U) != gneiss::result::success) {
    return 3;
  }
  gneiss_scene_instance scene{};
  constexpr std::string_view uri = "asset://scenes/scene.scene.json";
  const auto start = clock_type::now();
  auto result = GNEISS_SUCCESS;
  gneiss::application_internal::scene_load_completion completion;
  std::vector<double> event_intervals;
  double activation_elapsed_ms{};
  if (mode == "sync") {
    result = gneiss_scene_instance_load(app.get(), uri.data(), uri.size(), &scene);
  } else {
    using namespace gneiss::application_internal;
    result = attach_task_executor(app.get(), scheduler);
    std::uint64_t request{};
    if (result == GNEISS_SUCCESS) {
      result = request_scene_load(app.get(), uri, 1U, 1U, request);
    }
    auto previous = clock_type::now();
    const auto deadline = previous + std::chrono::minutes(15);
    update = [&](gneiss_application handle) {
      event_intervals.push_back(milliseconds(previous));
      previous = clock_type::now();
      if (clock_type::now() > deadline) {
        result = GNEISS_ERROR_NOT_READY;
        return gneiss_application_request_exit(handle);
      }
      if (mode == "cooperative") {
        (void)scheduler.run_ready();
      }
      bool finished{};
      if (result == GNEISS_SUCCESS) {
        result = poll_scene_load(handle, completion, finished);
      }
      if (finished || result != GNEISS_SUCCESS) {
        if (finished)
          result = completion.result;
        return gneiss_application_request_exit(handle);
      }
      scene_load_progress progress;
      bool active{};
      if (query_scene_load_progress(handle, progress, active) == GNEISS_SUCCESS && active &&
          progress.phase == scene_load_phase::ready) {
        result = activate_scene_load(handle, request, completion);
        scene = completion.scene;
        activation_elapsed_ms = milliseconds(start);
        return gneiss_application_request_exit(handle);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return GNEISS_SUCCESS;
    };
    if (result == GNEISS_SUCCESS) {
      const auto run_result = static_cast<gneiss_result>(app.run());
      if (run_result != GNEISS_SUCCESS)
        result = run_result;
      if (scene == GNEISS_NULL_SCENE_INSTANCE && result == GNEISS_SUCCESS)
        result = GNEISS_ERROR_INVALID_STATE;
    }
    update = {};
  }
  const auto load_ms = mode == "sync" ? milliseconds(start) : activation_elapsed_ms;
  std::printf("scene_load result=%d milliseconds=%.3f peak_resident_bytes=%llu\n", result, load_ms,
              static_cast<unsigned long long>(peak_resident_bytes()));
  std::fflush(stdout);
  if (result != GNEISS_SUCCESS) {
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
  report << std::setprecision(10) << "{\n  \"load_ms\": " << load_ms << ",\n  \"mode\": \"" << mode
         << "\""
         << ",\n  \"prepare_ms\": " << completion.prepare_ms
         << ",\n  \"asset_prepare_ms\": " << completion.asset_prepare_ms
         << ",\n  \"verify_ms\": " << completion.verify_ms
         << ",\n  \"upload_ms\": " << completion.upload_ms
         << ",\n  \"maximum_advance_ms\": " << completion.maximum_advance_ms
         << ",\n  \"activation_ms\": " << completion.activation_ms
         << ",\n  \"candidate_resident_bytes\": " << completion.progress.resident_bytes
         << ",\n  \"event_interval_p95_ms\": " << event_p95
         << ",\n  \"event_interval_max_ms\": " << event_max
         << ",\n  \"render_60_ticks_ms\": " << render_ms << ",\n  \"capture_ms\": " << capture_ms
         << ",\n  \"nodes\": " << nodes << ",\n  \"presented_frames\": " << stats.presented_frames
         << ",\n  \"peak_resident_bytes\": " << peak_resident_bytes() << "\n}\n";
  return pixels.good() && report.good() ? 0 : 9;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 10;
}
