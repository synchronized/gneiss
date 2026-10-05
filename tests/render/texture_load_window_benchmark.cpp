// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_asset_reload_internal.hpp"

#include <gneiss/application.hpp>
#include <gneiss/scene.h>

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
  tasks::task_scheduler* scheduler{};
  std::filesystem::path root;
  bool synchronous{};
  bool model{};
  std::vector<render_internal::render_asset_reload> assets;
  bool close_during_load{};
  gneiss_scene_instance scene{};
  gneiss_world world{};
  gneiss_scene_node_id camera{};
  gneiss_scene_node_id mesh{};
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point previous = start;
  bool requested{};
  bool completed{};
  unsigned resized{};
  bool minimized{};
  bool restored{};
  double prepare_ms{};
  double commit_ms{};
  double upload_ms{};
  double max_update_ms{};
  std::size_t candidate_bytes{};
  render_internal::render_queue_stats stats;
  std::vector<sample> samples;
};
gneiss_result update(gneiss_application app, const gneiss_frame_time*, void* opaque) {
  auto& state = *static_cast<context*>(opaque);
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(now - state.start).count();
  const auto interval = std::chrono::duration<double, std::milli>(now - state.previous).count();
  state.previous = now;
  if (elapsed >= 1.0) {
    state.max_update_ms = std::max(state.max_update_ms, interval);
  }
  if (state.scheduler->mode() == tasks::execution_mode::cooperative) {
    (void)state.scheduler->run_ready();
  }
  render_internal::render_queue_stats stats;
  if (application_internal::query_render_statistics(app, stats) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INTERNAL;
  }
  if (stats.presented_frames != state.stats.presented_frames) {
    state.samples.push_back(
        {elapsed, stats.presented_frames, stats.latest_frame_interval_ms, interval});
  }
  state.stats = stats;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[0] = static_cast<float>(std::sin(elapsed) * 0.15);
  transform.translation[2] = 2.0F;
  if (gneiss_scene_node_set_local_transform(state.world, state.camera, &transform) !=
      GNEISS_SUCCESS) {
    return GNEISS_ERROR_INTERNAL;
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
          << R"({"format":"gneiss.texture","version":1,"source":"asset://textures/after.png","color_space":"srgb"})";
    }
    state.requested = true;
    if (state.synchronous) {
      const std::vector<render_internal::render_asset_reload> reloads{
          {"asset://textures/test.texture.json", render_internal::render_asset_type::texture},
          {"asset://materials/test.material.json", render_internal::render_asset_type::material}};
      const auto start = std::chrono::steady_clock::now();
      auto result = application_internal::reload_render_assets(app, reloads);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      constexpr std::string_view mesh = "asset://models/test.mesh.json";
      constexpr std::string_view material = "asset://materials/test.material.json";
      const gneiss_scene_mesh_renderer_desc desc{.struct_size =
                                                     sizeof(gneiss_scene_mesh_renderer_desc),
                                                 .reserved = 0U,
                                                 .mesh_uri = mesh.data(),
                                                 .mesh_uri_length = mesh.size(),
                                                 .material_uri = material.data(),
                                                 .material_uri_length = material.size()};
      result = gneiss_scene_instance_set_mesh_renderer(app, state.scene, state.mesh, &desc);
      state.prepare_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      state.completed = result == GNEISS_SUCCESS;
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    } else {
      const std::vector<std::string> uris{"asset://textures/test.texture.json"};
      std::uint64_t request{};
      const auto result =
          state.model
              ? application_internal::request_render_assets(app, state.assets, 1U, 1U, request)
              : application_internal::request_textures(app, uris, 1U, 1U, request);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
  }
  if (state.requested && !state.completed) {
    render_internal::texture_load_completion completion;
    bool ready{};
    const auto result = application_internal::poll_textures(app, completion, ready);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    if (ready) {
      if (completion.result != GNEISS_SUCCESS) {
        return completion.result;
      }
      state.prepare_ms = completion.prepare_ms;
      state.commit_ms = completion.commit_ms;
      state.upload_ms = completion.upload_ms;
      state.candidate_bytes = completion.candidate_bytes;
      state.completed = true;
    }
  }
  if (state.close_during_load && state.requested && elapsed > 1.01) {
    return gneiss_application_request_exit(app);
  }
  if (elapsed > 4.0 && state.completed) {
    return gneiss_application_request_exit(app);
  }
  return elapsed > 20.0 ? GNEISS_ERROR_NOT_READY : GNEISS_SUCCESS;
}
}
int main(int argc, char** argv) try {
  if (argc != 4 && argc != 5) {
    return 64;
  }
  const std::string mode = argv[2];
  tasks::task_scheduler scheduler({.mode = mode == "cooperative"
                                               ? tasks::execution_mode::cooperative
                                               : tasks::execution_mode::thread_pool});
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
        state.assets.push_back({uri, render_internal::render_asset_type::mesh});
      if (name.ends_with(".material.json"))
        state.assets.push_back({uri, render_internal::render_asset_type::material});
    }
  }
  state.synchronous = mode == "sync";
  state.close_during_load = mode == "close";
  application app;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  const auto root = state.root.string();
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  desc.window_title = title;
  desc.window_title_length = static_cast<std::uint32_t>(std::char_traits<char>::length(title));
  desc.window_flags =
      GNEISS_APPLICATION_WINDOW_VISIBLE_BIT | GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT;
  desc.update = update;
  desc.user_data = &state;
  if (application::create(desc, app) != result::success ||
      application_internal::attach_task_executor(app.get(), scheduler) != GNEISS_SUCCESS) {
    return 1;
  }
  constexpr std::string_view scene = "asset://scenes/test.scene.json";
  if (gneiss_scene_instance_load(app.get(), scene.data(), scene.size(), &state.scene) !=
          GNEISS_SUCCESS ||
      gneiss_application_get_world(app.get(), &state.world) != GNEISS_SUCCESS) {
    return 2;
  }
  gneiss_scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  if (gneiss_scene_instance_get_node_info(app.get(), state.scene, 0U, &info) != GNEISS_SUCCESS) {
    return 3;
  }
  state.camera = info.node;
  if (gneiss_scene_instance_get_node_info(app.get(), state.scene, 1U, &info) != GNEISS_SUCCESS) {
    return 3;
  }
  state.mesh = info.node;
  state.start = state.previous = std::chrono::steady_clock::now();
  const auto result = app.run();
  app = application{};
  const auto lifetime =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - state.start).count();
  std::ofstream output(argv[3]);
  output << std::setprecision(10) << "elapsed,rendered,render_interval_ms,update_interval_ms\n";
  for (const auto& sample : state.samples) {
    output << sample.elapsed << ',' << sample.rendered << ',' << sample.render_interval << ','
           << sample.update_interval << '\n';
  }
  std::printf("mode=%s result=%d prepare_ms=%.3f commit_ms=%.3f upload_ms=%.3f max_update_ms=%.3f "
              "candidate_bytes=%zu rendered=%llu "
              "resize=%u restored=%u queue_peak=%zu\n",
              mode.c_str(), to_native(result), state.prepare_ms, state.commit_ms, state.upload_ms,
              state.max_update_ms, state.candidate_bytes,
              static_cast<unsigned long long>(state.stats.presented_frames), state.resized,
              static_cast<unsigned>(state.restored), state.stats.pending_high_watermark);
  std::printf("lifetime_seconds=%.3f completed=%u\n", lifetime,
              static_cast<unsigned>(state.completed));
  return result == result::success && (state.completed || state.close_during_load) &&
                 state.stats.presented_frames > 5U
             ? 0
             : 4;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 5;
}
