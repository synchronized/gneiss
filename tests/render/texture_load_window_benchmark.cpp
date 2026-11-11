// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"

#include "../support/scene_access.hpp"
#include "application/application.hpp"
#include "engine/function/scene/world.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace gneiss;
constexpr auto title = "Gneiss Texture Scheduling Benchmark";
struct sample {
  double elapsed{};
  std::uint64_t rendered{};
  double render_interval{};
  double update_interval{};
};
struct context {
  gneiss::asset_batch_request request;
  gneiss::tasks::task_scheduler* scheduler{};
  std::filesystem::path root;
  bool model{};
  std::vector<gneiss::render_resource_source> assets;
  bool close_during_load{};
  gneiss::scene_instance_id scene{};
  gneiss::entity_id camera{};
  gneiss::entity_id mesh{};
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point previous = start;
  bool requested{};
  bool completed{};
  unsigned resized{};
  bool minimized{};
  bool restored{};
  double prepare_ms{};
  double commit_ms{};
  double max_update_ms{};
  std::size_t prepared_bytes{};
  gneiss::render_queue_stats stats;
  std::vector<sample> samples;
};
gneiss::error native_update(runtime::application& app, context& state) {
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(now - state.start).count();
  const auto interval = std::chrono::duration<double, std::milli>(now - state.previous).count();
  state.previous = now;
  if (elapsed >= 1.0) {
    state.max_update_ms = std::max(state.max_update_ms, interval);
  }
  if (state.scheduler->mode() == gneiss::tasks::execution_mode::cooperative) {
    (void)state.scheduler->run_ready();
  }
  const auto stats = app.render_statistics();
  if (stats.presented_frames != state.stats.presented_frames) {
    state.samples.push_back(
        {elapsed, stats.presented_frames, stats.latest_frame_interval_ms, interval});
  }
  state.stats = stats;
  gneiss::math::transform transform;
  transform.translation[0] = static_cast<float>(std::sin(elapsed) * 0.15);
  transform.translation[2] = 2.0F;
  if (gneiss::error{app.native_world()
                                ->find(gneiss::entity_id{(state.camera).get()})
                                .set_local_transform(transform)} !=
      gneiss::error::success) {
    return gneiss::error::internal;
  }
#ifdef _WIN32
  const auto window = FindWindowA(nullptr, title);
  if (window && elapsed >= 1.0 && elapsed < 2.0 &&
      state.resized < static_cast<unsigned>((elapsed - 1.0) * 10.0)) {
    ++state.resized;
    SetWindowPos(window, nullptr, 0, 0, 800 + static_cast<int>(state.resized % 2U) * 100, 600,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (window && elapsed >= 1.15 && !state.minimized) {
    ShowWindow(window, SW_MINIMIZE);
    state.minimized = true;
  }
  if (window && elapsed >= 1.35 && !state.restored) {
    ShowWindow(window, SW_RESTORE);
    state.restored = true;
  }
#endif
  if (elapsed >= 1.0 && !state.requested) {
    if (!state.model) {
      std::ofstream stream(state.root / "textures/test.texture.json");
      stream
          << R"({"format":"gneiss.texture","version":2,"source":"asset://textures/after.png","color_space":"srgb","usage":"default"})";
    }
    state.requested = true;
    const std::array uris{gneiss::render_resource_source{
        .uri = "asset://textures/test.texture.json",
        .type = gneiss::render_resource_kind::texture}};
    auto result = state.model ? app.load_assets(state.assets, true) : app.load_assets(uris, true);
    if (!result)
      return result.error();
    state.request = std::move(*result);
  }
  if (state.requested && !state.completed) {
    gneiss::asset_load_completion completion;
    bool ready{};
    const auto result = gneiss::test_support::receive_request(state.request, completion, ready);
    if (result != gneiss::error::success) {
      return result;
    }
    if (ready) {
      if (completion.result != gneiss::error::success) {
        return completion.result;
      }
      state.prepare_ms = completion.prepare_ms;
      state.commit_ms = completion.commit_ms;
      state.prepared_bytes = completion.prepared_bytes;
      state.completed = true;
    }
  }
  if (state.close_during_load && state.requested && elapsed > 1.01) {
    app.request_exit();
    return gneiss::error::success;
  }
  if (elapsed > 4.0 && state.completed) {
    app.request_exit();
    return gneiss::error::success;
  }
  return elapsed > 20.0 ? gneiss::error::not_ready : gneiss::error::success;
}
}
int main(int argc, char** argv) try {
  if (argc != 4 && argc != 5) {
    return 64;
  }
  const std::string mode = argv[2];
  if (mode != "async" && mode != "cooperative" && mode != "close")
    return 64;
  gneiss::tasks::task_scheduler scheduler(
      {.mode = mode == "cooperative" ? gneiss::tasks::execution_mode::cooperative
                                     : gneiss::tasks::execution_mode::thread_pool});
  context state;
  state.scheduler = &scheduler;
  state.root = argv[1];
  state.model = argc == 5 && std::string_view(argv[4]) == "model";
  if (state.model) {
    for (const auto& file : std::filesystem::recursive_directory_iterator(state.root)) {
      if (!file.is_regular_file())
        continue;
      const auto name = file.path().filename().string();
      const auto uri =
          "asset://" + std::filesystem::relative(file.path(), state.root).generic_string();
      if (name.ends_with(".gneiss-mesh"))
        state.assets.push_back({uri, gneiss::render_resource_kind::mesh});
      if (name.ends_with(".material.json"))
        state.assets.push_back({uri, gneiss::render_resource_kind::material});
    }
  }
  state.close_during_load = mode == "close";
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration desc;
  const auto root = state.root.string();
  desc.enable_window = true;
  desc.asset_root = root;
  desc.window.title = title;
  desc.window.high_dpi = false;
  desc.callbacks.update = [&](const auto&) { return native_update(*app, state); };
  if (gneiss::test_support::assign_value(runtime::application::create(desc), app) !=
          gneiss::error::success ||
      app->attach_task_executor(scheduler) != gneiss::error::success) {
    return 1;
  }
  constexpr std::string_view scene = "asset://scenes/test.scene.json";
  if (gneiss::error{gneiss::test_support::load_scene(
          *app, scene, &state.scene, &scheduler)} != gneiss::error::success) {
    return 2;
  }
  gneiss::test_support::scene_node_info info;
  if (gneiss::error{gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*app}->get_node_info(state.scene, 0U), info)} !=
      gneiss::error::success) {
    return 3;
  }
  state.camera = info.node;
  if (gneiss::error{gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*app}->get_node_info(state.scene, 1U), info)} !=
      gneiss::error::success) {
    return 3;
  }
  state.mesh = info.node;
  state.start = state.previous = std::chrono::steady_clock::now();
  const auto result = gneiss::error{app->run(0U)};
  app.reset();
  const auto lifetime =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - state.start).count();
  std::ofstream output(argv[3]);
  output << std::setprecision(10) << "elapsed,rendered,render_interval_ms,update_interval_ms\n";
  for (const auto& sample : state.samples) {
    output << sample.elapsed << ',' << sample.rendered << ',' << sample.render_interval << ','
           << sample.update_interval << '\n';
  }
  std::printf("mode=%s result=%d prepare_ms=%.3f commit_ms=%.3f max_update_ms=%.3f "
              "prepared_bytes=%zu rendered=%llu "
              "resize=%u restored=%u queue_peak=%zu\n",
              mode.c_str(), (result).code(), state.prepare_ms, state.commit_ms, state.max_update_ms,
              state.prepared_bytes, static_cast<unsigned long long>(state.stats.presented_frames),
              state.resized, static_cast<unsigned>(state.restored),
              state.stats.pending_high_watermark);
  std::printf("lifetime_seconds=%.3f completed=%u\n", lifetime,
              static_cast<unsigned>(state.completed));
  return result == gneiss::error::success && (state.completed || state.close_during_load) &&
                 state.stats.presented_frames > 5U
             ? 0
             : 4;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 5;
}
