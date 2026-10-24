// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scope_exit.hpp"
#include "engine/core/result.hpp"
#include "engine/values/scene.hpp"
#include "runtime/application.hpp"
#include "runtime/application_bridge.hpp"
#include <gneiss/engine/application.h>
#include <gneiss/engine/world.h>

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
  gneiss_world world{};
  gneiss::scene_node_id camera_node;
  gneiss::engine::action_id orbit;
  gneiss::engine::action_id quit;
  double angle = 0.0;
  bool measure = false;
  std::chrono::steady_clock::time_point previous_update;
  std::array<double, measure_sample_frames> frame_times_ms{};
  std::size_t frame_time_count = 0U;
};

void report_failure(std::string_view stage, gneiss::engine::result result) {
  std::fprintf(stderr, "稳定运行时样例失败：阶段=%.*s，结果=%d，消息=%.*s\n",
               static_cast<int>(stage.size()), stage.data(), result.code(),
               static_cast<int>(result.message().size()), result.message().data());
}

gneiss::engine::result update_sample(gneiss::runtime::application& application,
                                     const gneiss::engine::frame_time& time,
                                     void* user_data) noexcept {
  if (user_data == nullptr) {
    return gneiss::engine::result::invalid_argument;
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
  gneiss::engine::action_state orbit{};
  gneiss::engine::action_state quit{};
  const auto orbit_result = application.get_action_state(state->orbit, orbit);
  if (orbit_result.failed()) {
    return orbit_result;
  }
  const auto quit_result = application.get_action_state(state->quit, quit);
  if (quit_result.failed()) {
    return quit_result;
  }
  if (quit.pressed) {
    application.request_exit();
    return gneiss::engine::result::success;
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
  const auto native = gneiss::to_native(transform);
  return gneiss::engine::result{
      gneiss_scene_node_set_local_transform(state->world, state->camera_node.get(), &native)};
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
  std::shared_ptr<gneiss::runtime::application> native_application;
  gneiss::runtime::application_bridge bridge;
  gneiss::runtime::application_configuration config;
  config.use_granit_window = true;
  config.window.title = title;
  config.asset_root = asset_root;
  config.callbacks.update = [&](auto, const auto& time) {
    return update_sample(*native_application, time, &state);
  };
  example::scope_exit close{[&] {
    if (native_application && native_application->accepts_requests())
      (void)native_application->shutdown(bridge.get());
  }};
  auto create_result = gneiss::runtime::application::create(config, native_application);
  if (create_result.ok())
    create_result = bridge.bind(native_application);
  if (create_result.failed()) {
    report_failure("创建 Application 或绑定兼容句柄", create_result);
    return 1;
  }
  const auto application = bridge.get();
  state.world = native_application->world();
  const auto application_ready = clock::now();
  gneiss_scene_instance scene{};
  example::scope_exit unload{[&] {
    if (scene)
      (void)gneiss_scene_instance_unload(application, scene);
  }};
  auto result = gneiss::engine::result{
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene)};
  if (result != gneiss::engine::result::success) {
    report_failure("加载场景", result);
    return 3;
  }
  const auto scene_ready = clock::now();
  result = native_application->load_action_map(input_map_uri);
  if (result != gneiss::engine::result::success) {
    report_failure("加载输入映射", result);
    return 4;
  }
  result = native_application->find_action("move_horizontal", state.orbit);
  if (result == gneiss::engine::result::success) {
    result = native_application->find_action("quit", state.quit);
  }
  if (result != gneiss::engine::result::success) {
    report_failure("查找输入动作", result);
    return 5;
  }
  result = ([&] {
    gneiss_scene_node_id node{};
    auto status = gneiss_scene_instance_find_node(application, scene, camera_uuid.data(),
                                                  camera_uuid.size(), &node);
    if (status == GNEISS_SUCCESS)
      state.camera_node = gneiss::scene_node_id{node};
    return gneiss::engine::result{status};
  })();
  if (result != gneiss::engine::result::success) {
    report_failure("查找 Camera", result);
    return 6;
  }
  const auto setup_ready = clock::now();

  state.previous_update = clock::now();
  std::uint64_t frame_count = smoke ? 3U : 0U;
  if (measure) {
    frame_count = measure_warmup_frames + measure_sample_frames;
  }
  result = native_application->run(application, frame_count);
  if (result != gneiss::engine::result::success) {
    report_failure("运行主循环", result);
    return 7;
  }
  const auto run_finished = clock::now();
  result = ([&] {
    auto status = gneiss_scene_instance_unload(application, scene);
    if (status == GNEISS_SUCCESS)
      scene = 0;
    return gneiss::engine::result{status};
  })();
  if (result != gneiss::engine::result::success) {
    report_failure("卸载场景", result);
    return 8;
  }
  const auto scene_unloaded = clock::now();
  result = native_application->shutdown(application);
  if (result.failed()) {
    report_failure("关闭 Application", result);
    return 10;
  }
  result = bridge.reset();
  if (result.failed()) {
    report_failure("撤销 Application 兼容句柄", result);
    return 10;
  }
  native_application.reset();
  const auto application_destroyed = clock::now();
  if (measure) {
    if (state.frame_time_count != measure_sample_frames) {
      report_failure("采集稳定帧", gneiss::engine::result::internal);
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
