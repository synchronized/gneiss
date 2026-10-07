// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/scene.hpp>

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
  gneiss::world_ref world;
  gneiss::scene_node_id camera_node;
  gneiss::action_id orbit;
  gneiss::action_id quit;
  double angle = 0.0;
  bool measure = false;
  std::chrono::steady_clock::time_point previous_update;
  std::array<double, measure_sample_frames> frame_times_ms{};
  std::size_t frame_time_count = 0U;
};

void report_failure(std::string_view stage, gneiss::result result) {
  std::fprintf(stderr, "稳定运行时样例失败：阶段=%.*s，结果=%d，消息=%.*s\n",
               static_cast<int>(stage.size()), stage.data(), result.native(),
               static_cast<int>(result.message().size()), result.message().data());
}

gneiss::result update_sample(gneiss::application_ref application, const gneiss::frame_time& time,
                             void* user_data) noexcept {
  if (user_data == nullptr) {
    return gneiss::result::invalid_argument;
  }

  auto* state = static_cast<sample_state*>(user_data);
  if (state->measure) {
    const auto current_update = std::chrono::steady_clock::now();
    if (time.frame_index >= measure_warmup_frames &&
        state->frame_time_count < state->frame_times_ms.size()) {
      state->frame_times_ms[state->frame_time_count] =
          std::chrono::duration<double, std::milli>(current_update - state->previous_update)
              .count();
      ++state->frame_time_count;
    }
    state->previous_update = current_update;
  }
  gneiss::action_state orbit{};
  gneiss::action_state quit{};
  const auto orbit_result = application.get_action_state(state->orbit, orbit);
  if (orbit_result.failed()) {
    return orbit_result;
  }
  const auto quit_result = application.get_action_state(state->quit, quit);
  if (quit_result.failed()) {
    return quit_result;
  }
  if (quit.pressed) {
    return application.request_exit();
  }

  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  constexpr double radius = 6.0;
  constexpr double height = 2.3;
  state->angle += static_cast<double>(time.delta_ns) / nanoseconds_per_second * orbit.value;
  const auto yaw_half = state->angle * 0.5;
  const auto pitch_half = std::atan2(-height, radius) * 0.5;

  gneiss::transform transform;
  transform.translation[0] = static_cast<float>(std::sin(state->angle) * radius);
  transform.translation[1] = static_cast<float>(height);
  transform.translation[2] = static_cast<float>(std::cos(state->angle) * radius);
  transform.rotation[0] = static_cast<float>(std::cos(yaw_half) * std::sin(pitch_half));
  transform.rotation[1] = static_cast<float>(std::sin(yaw_half) * std::cos(pitch_half));
  transform.rotation[2] = static_cast<float>(-std::sin(yaw_half) * std::sin(pitch_half));
  transform.rotation[3] = static_cast<float>(std::cos(yaw_half) * std::cos(pitch_half));
  return state->world.set_local_transform(state->camera_node, transform);
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
  const gneiss::application_desc desc{
      .callbacks = {.user_data = &state, .update = update_sample},
      .platform = gneiss::application_platform::granit,
      .window_title = title,
      .asset_root = asset_root,
  };

  gneiss::application application;
  const auto create_result = gneiss::application::create(desc, application);
  if (create_result != gneiss::result::success) {
    report_failure("创建 Application", create_result);
    return 1;
  }
  const auto application_ready = clock::now();
  const auto world_result = application.get_world(state.world);
  if (world_result != gneiss::result::success) {
    report_failure("获取 World", world_result);
    return 2;
  }

  gneiss::scene_instance scene;
  auto result = gneiss::scene_instance::load(application.get(), scene_uri, scene);
  if (result != gneiss::result::success) {
    report_failure("加载场景", result);
    return 3;
  }
  const auto scene_ready = clock::now();
  result = application.load_action_map(input_map_uri);
  if (result != gneiss::result::success) {
    report_failure("加载输入映射", result);
    return 4;
  }
  result = application.find_action("move_horizontal", state.orbit);
  if (result == gneiss::result::success) {
    result = application.find_action("quit", state.quit);
  }
  if (result != gneiss::result::success) {
    report_failure("查找输入动作", result);
    return 5;
  }
  result = scene.find_node(camera_uuid, state.camera_node);
  if (result != gneiss::result::success) {
    report_failure("查找 Camera", result);
    return 6;
  }
  const auto setup_ready = clock::now();

  state.previous_update = clock::now();
  std::uint64_t frame_count = smoke ? 3U : 0U;
  if (measure) {
    frame_count = measure_warmup_frames + measure_sample_frames;
  }
  result = application.run(frame_count);
  if (result != gneiss::result::success) {
    report_failure("运行主循环", result);
    return 7;
  }
  const auto run_finished = clock::now();
  result = scene.reset();
  if (result != gneiss::result::success) {
    report_failure("卸载场景", result);
    return 8;
  }
  const auto scene_unloaded = clock::now();
  result = application.reset();
  if (result.failed()) {
    report_failure("关闭 Application", result);
    return 10;
  }
  const auto application_destroyed = clock::now();
  if (measure) {
    if (state.frame_time_count != measure_sample_frames) {
      report_failure("采集稳定帧", gneiss::result::internal);
      return 9;
    }
    const auto [minimum, maximum] = std::ranges::minmax_element(state.frame_times_ms);
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
