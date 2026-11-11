// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scene_activation.hpp"
#include "../support/scope_exit.hpp"
#include "application/application.hpp"
#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/world.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct example_state {
  gneiss::entity_id camera_node{};
  gneiss::action_id orbit{};
  gneiss::action_id quit{};
  double angle = 0.0;
};

gneiss::error update_gallery(gneiss::runtime::application& application,
                                     const gneiss::frame_time& time, void* user_data) {
  if (user_data == nullptr) {
    return gneiss::error::invalid_argument;
  }
  auto* state = static_cast<example_state*>(user_data);
  gneiss::action_state orbit{};
  gneiss::action_state quit{};
  if (application.get_action_state(state->orbit, orbit).failed() ||
      application.get_action_state(state->quit, quit).failed()) {
    return gneiss::error::invalid_state;
  }
  if (quit.pressed) {
    application.request_exit();
    return gneiss::error::success;
  }
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  state->angle += static_cast<double>(time.delta_ns) / nanoseconds_per_second * orbit.value;
  constexpr double radius = 8.0;
  constexpr double height = 3.2;
  const auto yaw_half = state->angle * 0.5;
  const auto pitch_half = std::atan2(-height, radius) * 0.5;
  gneiss::math::transform transform{};
  transform.translation[0] = static_cast<float>(std::sin(state->angle) * radius);
  transform.translation[1] = static_cast<float>(height);
  transform.translation[2] = static_cast<float>(std::cos(state->angle) * radius);
  transform.rotation[0] = static_cast<float>(std::cos(yaw_half) * std::sin(pitch_half));
  transform.rotation[1] = static_cast<float>(std::sin(yaw_half) * std::cos(pitch_half));
  transform.rotation[2] = static_cast<float>(-std::sin(yaw_half) * std::sin(pitch_half));
  transform.rotation[3] = static_cast<float>(std::cos(yaw_half) * std::cos(pitch_half));
  return application.native_world()
      ->find(gneiss::entity_id{(state->camera_node).get()})
      .set_local_transform(transform);
}

} // namespace

int run_example(int argc, char** argv) {
  const auto arguments = std::span{argv + 1, static_cast<std::size_t>(argc - 1)};
  const auto has_argument = [&](std::string_view expected) {
    return std::ranges::any_of(arguments,
                               [expected](const char* value) { return expected == value; });
  };
  const bool smoke = has_argument("--smoke");
  const bool profile = has_argument("--profile");
  using clock = std::chrono::steady_clock;
  const auto started = clock::now();
  example_state state;
  constexpr std::string_view title = "Gneiss Lantern Gallery";
  constexpr std::string_view scene_uri = "asset://scenes/gallery.scene.json";
  constexpr std::string_view input_map_uri = "asset://input/default.input-map.json";
  const auto camera_uuid = *gneiss::guid::parse("20000000-0000-4000-8000-000000000001");
  std::error_code path_error;
  const auto executable = std::filesystem::absolute(argv[0], path_error);
  const auto installed_asset_root =
      executable.parent_path().parent_path() / "share/gneiss/examples/lantern-gallery/assets";
  const auto has_installed_assets =
      !path_error && std::filesystem::is_directory(installed_asset_root, path_error);
  const auto asset_root = has_installed_assets && !path_error
                              ? installed_asset_root.string()
                              : std::string{GNEISS_LANTERN_GALLERY_ASSET_ROOT};
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  bool can_update = false;
  std::shared_ptr<gneiss::runtime::application> native_application;
  gneiss::runtime::application_configuration config;
  config.enable_window = true;
  config.window.title = title;
  config.asset_root = asset_root;
  config.callbacks.update = [&](const auto& time) {
    (void)scheduler.run_ready();
    if (!can_update)
      return gneiss::error::success;
    return update_gallery(*native_application, time, &state);
  };
  example::scope_exit close{[&] {
    if (native_application && native_application->accepts_requests())
      (void)native_application->shutdown();
  }};
  auto create_result = gneiss::runtime::application::create(config);
  if (!create_result) {
    return 1;
  }
  const auto application_ready = clock::now();
  native_application = std::move(*create_result);
  if (native_application->attach_task_executor(scheduler).failed() ||
      native_application->begin_action_map(input_map_uri).failed())
    return 2;
  auto submitted = native_application->assets().load<gneiss::scene>(scene_uri);
  if (!submitted)
    return 2;
  auto scene_request = std::move(*submitted);
  gneiss::scene_instance_id scene{};
  example::scene_activation completion;
  // 启动阶段继续处理窗口事件和文件完成；不调用同步场景加载。
  while (!scene || native_application->action_map_status() == gneiss::error::not_ready ||
         (completion.visuals && !completion.visuals.ready())) {
    (void)scheduler.run_ready();
    if (native_application->run(1).failed())
      return 2;
    if (!scene) {
      const auto status = scene_request.status();
      if (status.failed() && status != gneiss::error::not_ready)
        return 2;
      if (status.ok()) {
        auto content = scene_request.snapshot();
        auto loaded = scene_request.take();
        if (!loaded || !content)
          return 2;
        auto activated = example::activate_scene(*native_application, *content);
        if (!activated)
          return 2;
        completion = std::move(*activated);
        scene = completion.instance.instance;
      }
    }
  }
  if (native_application->action_map_status().failed())
    return 2;
  example::report_visuals(completion);
  const auto scene_ready = clock::now();
  if (native_application->action_map_status() != gneiss::error::success ||
      native_application->find_action("move_horizontal", state.orbit) !=
          gneiss::error::success ||
      native_application->find_action("quit", state.quit) != gneiss::error::success) {
    return 2;
  }
  const auto input_ready = clock::now();
  auto instance =
      gneiss::scene_instance::borrow(*native_application->native_world(), scene);
  if (!instance)
    return 2;
  auto binding =
      std::ranges::find_if(completion.instance.objects, [camera_uuid](const auto& object) {
        return object.object == camera_uuid && !object.source;
      });
  if (binding == completion.instance.objects.end())
    return 2;
  state.camera_node = binding->entity;
  can_update = true;
  if (native_application->run(smoke ? 3U : 0U) != gneiss::error::success) {
    return 3;
  }
  const auto run_finished = clock::now();
  if (profile) {
    const auto milliseconds = [](auto begin, auto end) {
      return std::chrono::duration<double, std::milli>(end - begin).count();
    };
    std::cout << "Application 初始化：" << milliseconds(started, application_ready) << " ms\n"
              << "Scene 与资产加载：" << milliseconds(application_ready, scene_ready) << " ms\n"
              << "输入初始化：" << milliseconds(scene_ready, input_ready) << " ms\n"
              << "运行阶段：" << milliseconds(input_ready, run_finished) << " ms\n";
  }
  return instance->unload() == gneiss::error::success ? 0 : 4;
}

int main(int argc, char** argv) {
  gneiss::install_fatal_handler();
  try {
    return run_example(argc, argv);
  } catch (...) {
    gneiss::fatal_current_exception();
  }
}
