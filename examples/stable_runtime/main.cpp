// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/input.hpp>
#include <gneiss/scene.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint64_t measure_warmup_frames = 60U;
constexpr std::uint64_t measure_sample_frames = 300U;

struct sample_state {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_action orbit = GNEISS_NULL_ACTION;
  gneiss_action quit = GNEISS_NULL_ACTION;
  double angle = 0.0;
  bool measure = false;
  std::chrono::steady_clock::time_point previous_update{};
  std::array<double, measure_sample_frames> frame_times_ms{};
  std::size_t frame_time_count = 0U;
};

void report_failure(std::string_view stage, gneiss_result result) {
  std::fprintf(stderr, "稳定运行时样例失败：阶段=%.*s，结果=%d，消息=%s\n",
               static_cast<int>(stage.size()), stage.data(), result, gneiss_result_message(result));
}

gneiss_result update_sample(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  auto* state = static_cast<sample_state*>(user_data);
  if (state->measure) {
    const auto current_update = std::chrono::steady_clock::now();
    if (time->frame_index >= measure_warmup_frames &&
        state->frame_time_count < state->frame_times_ms.size()) {
      state->frame_times_ms[state->frame_time_count] =
          std::chrono::duration<double, std::milli>(current_update - state->previous_update)
              .count();
      ++state->frame_time_count;
    }
    state->previous_update = current_update;
  }
  gneiss_action_state orbit = GNEISS_ACTION_STATE_INIT;
  gneiss_action_state quit = GNEISS_ACTION_STATE_INIT;
  const auto orbit_result = gneiss_application_get_action_state(application, state->orbit, &orbit);
  if (orbit_result != GNEISS_SUCCESS) {
    return orbit_result;
  }
  const auto quit_result = gneiss_application_get_action_state(application, state->quit, &quit);
  if (quit_result != GNEISS_SUCCESS) {
    return quit_result;
  }
  if (quit.pressed != 0U) {
    return gneiss_application_request_exit(application);
  }

  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  constexpr double radius = 6.0;
  constexpr double height = 2.3;
  state->angle += static_cast<double>(time->delta_ns) / nanoseconds_per_second * orbit.value;
  const auto yaw_half = state->angle * 0.5;
  const auto pitch_half = std::atan2(-height, radius) * 0.5;

  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[0] = static_cast<float>(std::sin(state->angle) * radius);
  transform.translation[1] = static_cast<float>(height);
  transform.translation[2] = static_cast<float>(std::cos(state->angle) * radius);
  transform.rotation[0] = static_cast<float>(std::cos(yaw_half) * std::sin(pitch_half));
  transform.rotation[1] = static_cast<float>(std::sin(yaw_half) * std::cos(pitch_half));
  transform.rotation[2] = static_cast<float>(-std::sin(yaw_half) * std::sin(pitch_half));
  transform.rotation[3] = static_cast<float>(std::cos(yaw_half) * std::cos(pitch_half));
  return gneiss_scene_node_set_local_transform(state->world, state->camera_node, &transform);
}

double milliseconds(std::chrono::steady_clock::time_point begin,
                    std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

double percentile(const std::array<double, measure_sample_frames>& samples, std::size_t count,
                  double fraction) {
  auto sorted = samples;
  std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
  const auto index = static_cast<std::size_t>(fraction * static_cast<double>(count - 1U));
  return sorted[index];
}

int run_sample(std::string_view executable_path, bool smoke, bool measure) {
  constexpr std::string_view title = "Gneiss Stable Runtime Sample";
  constexpr std::string_view scene_uri = "asset://scenes/temple.scene.json";
  constexpr std::string_view input_map_uri = "asset://input/default.input-map.json";
  constexpr std::string_view camera_uuid = "10000000-0000-4000-8000-000000000002";

  std::error_code path_error;
  const auto executable = std::filesystem::absolute(executable_path, path_error);
  const auto installed_asset_root =
      executable.parent_path().parent_path() / "share/gneiss/examples/stable-runtime/assets";
  const auto has_installed_assets =
      !path_error && std::filesystem::is_directory(installed_asset_root, path_error);
  const auto asset_root = has_installed_assets && !path_error
                              ? installed_asset_root.string()
                              : std::string{GNEISS_STABLE_RUNTIME_ASSET_ROOT};

  using clock = std::chrono::steady_clock;
  const auto started = clock::now();
  sample_state state;
  state.measure = measure;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_sample;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());

  gneiss::application application;
  const auto create_result = gneiss::application::create(desc, application);
  if (create_result != gneiss::result::success) {
    report_failure("创建 Application", static_cast<gneiss_result>(create_result));
    return 1;
  }
  const auto application_ready = clock::now();
  const auto world_result = application.get_world(state.world);
  if (world_result != gneiss::result::success) {
    report_failure("获取 World", static_cast<gneiss_result>(world_result));
    return 2;
  }

  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  auto result =
      gneiss_scene_instance_load(application.get(), scene_uri.data(), scene_uri.size(), &scene);
  if (result != GNEISS_SUCCESS) {
    report_failure("加载场景", result);
    return 3;
  }
  const auto scene_ready = clock::now();
  result = static_cast<gneiss_result>(gneiss::load_action_map(application.get(), input_map_uri));
  if (result != GNEISS_SUCCESS) {
    report_failure("加载输入映射", result);
    return 4;
  }
  result = static_cast<gneiss_result>(
      gneiss::find_action(application.get(), "move_horizontal", state.orbit));
  if (result == GNEISS_SUCCESS) {
    result = static_cast<gneiss_result>(gneiss::find_action(application.get(), "quit", state.quit));
  }
  if (result != GNEISS_SUCCESS) {
    report_failure("查找输入动作", result);
    return 5;
  }
  result = gneiss_scene_instance_find_node(application.get(), scene, camera_uuid.data(),
                                           camera_uuid.size(), &state.camera_node);
  if (result != GNEISS_SUCCESS) {
    report_failure("查找 Camera", result);
    return 6;
  }
  const auto setup_ready = clock::now();

  state.previous_update = clock::now();
  const auto frame_count =
      measure ? measure_warmup_frames + measure_sample_frames : (smoke ? UINT64_C(3) : UINT64_C(0));
  result = static_cast<gneiss_result>(application.run(frame_count));
  if (result != GNEISS_SUCCESS) {
    report_failure("运行主循环", result);
    return 7;
  }
  const auto run_finished = clock::now();
  result = gneiss_scene_instance_unload(application.get(), scene);
  if (result != GNEISS_SUCCESS) {
    report_failure("卸载场景", result);
    return 8;
  }
  const auto scene_unloaded = clock::now();
  application.reset();
  const auto application_destroyed = clock::now();
  if (measure) {
    if (state.frame_time_count != measure_sample_frames) {
      report_failure("采集稳定帧", GNEISS_ERROR_INTERNAL);
      return 9;
    }
    const auto [minimum, maximum] =
        std::minmax_element(state.frame_times_ms.begin(), state.frame_times_ms.end());
    std::printf("{\"schema\":1,\"warmup_frames\":%llu,\"sample_frames\":%llu,"
                "\"application_create_ms\":%.3f,\"scene_load_ms\":%.3f,\"setup_ms\":%.3f,"
                "\"run_ms\":%.3f,\"scene_unload_ms\":%.3f,\"application_destroy_ms\":%.3f,"
                "\"total_ms\":%.3f,\"frame_ms_min\":%.3f,\"frame_ms_median\":%.3f,"
                "\"frame_ms_p95\":%.3f,\"frame_ms_max\":%.3f}\n",
                static_cast<unsigned long long>(measure_warmup_frames),
                static_cast<unsigned long long>(measure_sample_frames),
                milliseconds(started, application_ready),
                milliseconds(application_ready, scene_ready),
                milliseconds(scene_ready, setup_ready), milliseconds(setup_ready, run_finished),
                milliseconds(run_finished, scene_unloaded),
                milliseconds(scene_unloaded, application_destroyed),
                milliseconds(started, application_destroyed), *minimum,
                percentile(state.frame_times_ms, state.frame_time_count, 0.5),
                percentile(state.frame_times_ms, state.frame_time_count, 0.95), *maximum);
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--smoke" &&
                   std::string_view{argv[1]} != "--measure")) {
    std::fprintf(stderr, "用法：gneiss_stable_runtime_consumer [--smoke|--measure]\n");
    return 64;
  }
  try {
    const auto argument = argc == 2 ? std::string_view{argv[1]} : std::string_view{};
    return run_sample(argv[0], argument == "--smoke", argument == "--measure");
  } catch (...) {
    std::fprintf(stderr, "稳定运行时样例失败：阶段=未处理异常\n");
    return 99;
  }
}
