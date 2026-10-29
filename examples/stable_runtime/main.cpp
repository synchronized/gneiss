// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scope_exit.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

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
  gneiss::scene_node_id camera_node;
  gneiss::engine::action_id orbit;
  gneiss::engine::action_id quit;
  double angle = 0.0;
  bool measure = false;
  std::uint64_t measured_updates{};
  std::chrono::steady_clock::time_point previous_update;
  std::array<double, measure_sample_frames> frame_times_ms{};
  std::size_t frame_time_count = 0U;
};

void report_failure(std::string_view stage, gneiss::engine::error result) {
  std::fprintf(stderr, "稳定运行时样例失败：阶段=%.*s，结果=%d，消息=%.*s\n",
               static_cast<int>(stage.size()), stage.data(), result.code(),
               static_cast<int>(result.message().size()), result.message().data());
}

gneiss::engine::error update_sample(gneiss::runtime::application& application,
                                    const gneiss::engine::frame_time& time,
                                    void* user_data) noexcept {
  if (user_data == nullptr) {
    return gneiss::engine::error::invalid_argument;
  }

  auto* state = static_cast<sample_state*>(user_data);
  if (state->measure) {
    const auto current_update = std::chrono::steady_clock::now();
    if (state->measured_updates++ >= measure_warmup_frames &&
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
    return gneiss::engine::error::success;
  }

  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  constexpr double radius = 6.0;
  constexpr double height = 2.3;
  state->angle += static_cast<double>(time.delta_ns) / nanoseconds_per_second * orbit.value;
  const auto yaw_half = state->angle * 0.5;
  const auto pitch_half = std::atan2(-height, radius) * 0.5;

  gneiss::engine::math::transform transform;
  transform.translation[0] = static_cast<float>(std::sin(state->angle) * radius);
  transform.translation[1] = static_cast<float>(height);
  transform.translation[2] = static_cast<float>(std::cos(state->angle) * radius);
  transform.rotation[0] = static_cast<float>(std::cos(yaw_half) * std::sin(pitch_half));
  transform.rotation[1] = static_cast<float>(std::sin(yaw_half) * std::cos(pitch_half));
  transform.rotation[2] = static_cast<float>(-std::sin(yaw_half) * std::sin(pitch_half));
  transform.rotation[3] = static_cast<float>(std::cos(yaw_half) * std::cos(pitch_half));
  return application.native_world()->node_set_local_transform(state->camera_node, transform);
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
  const auto camera_uuid = *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000002");

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
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::engine::execution_mode::cooperative});
  bool can_update = false;
  std::shared_ptr<gneiss::runtime::application> native_application;
  gneiss::runtime::application_configuration config;
  config.enable_window = true;
  config.window.title = title;
  config.asset_root = asset_root;
  config.callbacks.update = [&](const auto& time) {
    (void)scheduler.run_ready();
    if (!can_update)
      return gneiss::engine::error::success;
    return update_sample(*native_application, time, &state);
  };
  example::scope_exit close{[&] {
    if (native_application && native_application->accepts_requests())
      (void)native_application->shutdown();
  }};
  auto create_result = gneiss::runtime::application::create(config, native_application);
  if (create_result.failed()) {
    report_failure("创建 Application", create_result);
    return 1;
  }
  const auto application_ready = clock::now();
  if (native_application->attach_task_executor(scheduler).failed() ||
      native_application->begin_action_map(input_map_uri).failed())
    return 2;
  std::uint64_t scene_request{};
  if (native_application->request_scene(scene_uri, 1, 1, scene_request).failed())
    return 2;
  gneiss::scene_instance_id scene{};
  gneiss::runtime::scene_load_completion completion;
  // 启动阶段继续处理窗口事件和文件完成；不调用同步场景加载。
  while (!scene || native_application->action_map_status() == gneiss::engine::error::not_ready) {
    (void)scheduler.run_ready();
    if (native_application->run(1).failed())
      return 2;
    if (!scene) {
      bool finished = false;
      if (native_application->poll_scene(completion, finished).failed() ||
          (finished && completion.result.failed()))
        return 2;
      gneiss::runtime::scene_load_progress progress;
      if (native_application->scene_progress(progress) &&
          progress.phase == gneiss::runtime::scene_load_phase::ready &&
          native_application->can_activate_scene()) {
        if (native_application->activate_scene(scene_request, completion).failed())
          return 2;
        scene = completion.scene;
      }
    }
  }
  if (native_application->action_map_status().failed())
    return 2;
  example::scope_exit unload{[&] {
    if (scene)
      (void)native_application->scenes()->unload(scene);
  }};
  const auto scene_ready = clock::now();
  auto result = native_application->action_map_status();
  if (result != gneiss::engine::error::success) {
    report_failure("加载输入映射", result);
    return 4;
  }
  result = native_application->find_action("move_horizontal", state.orbit);
  if (result == gneiss::engine::error::success) {
    result = native_application->find_action("quit", state.quit);
  }
  if (result != gneiss::engine::error::success) {
    report_failure("查找输入动作", result);
    return 5;
  }
  result = native_application->scenes()->find_node(scene, camera_uuid, state.camera_node);
  if (result != gneiss::engine::error::success) {
    report_failure("查找 Camera", result);
    return 6;
  }
  const auto setup_ready = clock::now();

  can_update = true;
  state.previous_update = clock::now();
  std::uint64_t frame_count = smoke ? 3U : 0U;
  if (measure) {
    frame_count = measure_warmup_frames + measure_sample_frames;
  }
  result = native_application->run(frame_count);
  if (result != gneiss::engine::error::success) {
    report_failure("运行主循环", result);
    return 7;
  }
  const auto run_finished = clock::now();
  result = ([&] {
    auto status = native_application->scenes()->unload(scene);
    if (status == gneiss::engine::error::success)
      scene = {};
    return gneiss::engine::error{status};
  })();
  if (result != gneiss::engine::error::success) {
    report_failure("卸载场景", result);
    return 8;
  }
  const auto scene_unloaded = clock::now();
  result = native_application->shutdown();
  if (result.failed()) {
    report_failure("关闭 Application", result);
    return 10;
  }
  native_application.reset();
  const auto application_destroyed = clock::now();
  if (measure) {
    if (state.frame_time_count != measure_sample_frames) {
      report_failure("采集稳定帧", gneiss::engine::error::internal);
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
