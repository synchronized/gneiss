// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scene_activation.hpp"
#include "../support/scope_exit.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/function/scene/world.hpp>

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
  gneiss::entity_id camera_node;
  gneiss::action_id orbit;
  gneiss::action_id quit;
  double angle = 0.0;
  bool measure = false;
  std::array<gneiss::rid, 2> targets{};
  bool captured_dual{};
  std::uint64_t measured_updates{};
  std::chrono::steady_clock::time_point previous_update;
  std::array<double, measure_sample_frames> frame_times_ms{};
  std::size_t frame_time_count = 0U;
};

void report_failure(std::string_view stage, gneiss::error result) {
  std::fprintf(stderr, "场景查看器失败：阶段=%.*s，结果=%d，消息=%.*s\n",
               static_cast<int>(stage.size()), stage.data(), result.code(),
               static_cast<int>(result.message().size()), result.message().data());
}

gneiss::error update_sample(gneiss::runtime::application& application,
                            const gneiss::frame_time& time, void* user_data) noexcept {
  if (user_data == nullptr) {
    return gneiss::error::invalid_argument;
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
    application.request_exit();
    return gneiss::error::success;
  }

  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  constexpr double radius = 6.0;
  constexpr double height = 2.3;
  state->angle += static_cast<double>(time.delta_ns) / nanoseconds_per_second * orbit.value;
  const auto yaw_half = state->angle * 0.5;
  const auto pitch_half = std::atan2(-height, radius) * 0.5;

  gneiss::math::transform transform;
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

// 两个视图共享应用的 World；右侧固定独立相机，左侧继续使用场景主相机。
gneiss::result<void> configure_dual_view(gneiss::runtime::application& application,
                                         const std::array<gneiss::rid, 2>& targets) {
  using namespace gneiss;
  view_camera camera;
  camera.transform.translation = {0.0F, 3.0F, 9.0F};
  const float half_pitch = std::atan2(-3.0F, 9.0F) * 0.5F;
  camera.transform.rotation = math::quaternion{std::cos(half_pitch), std::sin(half_pitch), 0, 0};
  const std::array views{runtime::application_view{.target = targets[0]},
                         runtime::application_view{.target = targets[1], .camera = camera}};
  auto status = application.set_views(views);
  if (!status)
    return status;
  std::uint32_t width{}, height{};
  auto size_status = application.get_window_size(width, height);
  if (size_status.failed())
    return size_status;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  const std::array vertices{
      ui_vertex{{0, 0}, {0, 0}, 0xffffffffU},     ui_vertex{{w / 2, 0}, {1, 0}, 0xffffffffU},
      ui_vertex{{w / 2, h}, {1, 1}, 0xffffffffU}, ui_vertex{{0, h}, {0, 1}, 0xffffffffU},
      ui_vertex{{w / 2, 0}, {0, 0}, 0xffffffffU}, ui_vertex{{w, 0}, {1, 0}, 0xffffffffU},
      ui_vertex{{w, h}, {1, 1}, 0xffffffffU},     ui_vertex{{w / 2, h}, {0, 1}, 0xffffffffU}};
  const std::array<std::uint32_t, 12> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
  const std::array commands{
      ui_draw_command{.texture = ui_target_image{targets[0]}, .clip_max = {w, h}, .index_count = 6},
      ui_draw_command{.texture = ui_target_image{targets[1]},
                      .clip_max = {w, h},
                      .first_index = 6,
                      .index_count = 6}};
  auto drawn = application.submit_ui_draw_list({.display_width = w,
                                                .display_height = h,
                                                .vertices = vertices,
                                                .indices = indices,
                                                .commands = commands});
  return drawn.failed() ? result<void>{drawn} : result<void>{};
}

// 回读窗口合成结果：两侧均须存在图像变化，且两个相机不能产生相同画面。
bool has_distinct_views(const gneiss::render::frame_image& image) {
  if (image.width != 512U || image.height != 256U || image.pixels.size() != 512U * 256U * 4U)
    return false;
  bool left_varies = false, right_varies = false, differs = false;
  for (std::size_t y = 1; y < image.height - 1U; ++y) {
    for (std::size_t x = 1; x < image.width / 2U - 1U; ++x) {
      const auto left = (y * image.width + x) * 4U;
      const auto right = left + image.width / 2U * 4U;
      for (std::size_t c = 0; c < 3U; ++c) {
        left_varies |= image.pixels[left + c] != image.pixels[c];
        right_varies |= image.pixels[right + c] != image.pixels[image.width / 2U * 4U + c];
        differs |= image.pixels[left + c] != image.pixels[right + c];
      }
    }
  }
  return left_varies && right_varies && differs;
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

int run_sample(std::string_view executable_path, bool smoke, bool measure, bool dual_view,
               const std::string& asset_override, const std::string& scene_uri,
               const std::string& input_map_uri) {
  constexpr std::string_view title = "Gneiss Scene Viewer";

  std::error_code path_error;
  const auto executable = std::filesystem::absolute(executable_path, path_error);
  const auto installed_asset_root =
      executable.parent_path().parent_path() / "share/gneiss/examples/scene-viewer/assets";
  const auto has_installed_assets =
      !path_error && std::filesystem::is_directory(installed_asset_root, path_error);
  const auto asset_root = !asset_override.empty() ? asset_override
                          : has_installed_assets && !path_error
                              ? installed_asset_root.string()
                              : std::string{GNEISS_VIEWER_ASSET_ROOT};

  using clock = std::chrono::steady_clock;
  const auto started = clock::now();
  sample_state state;
  state.measure = measure;
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
    auto updated = update_sample(*native_application, time, &state);
    if (updated.failed() || !state.targets[0])
      return updated;
    auto configured = configure_dual_view(*native_application, state.targets);
    if (!configured)
      return configured.error();
    if (smoke && !state.captured_dual) {
      gneiss::render::frame_image image;
      auto captured = native_application->capture_frame(512, 256, image);
      if (captured == gneiss::error::not_ready)
        return gneiss::error{gneiss::error::success};
      if (captured.failed())
        return captured;
      if (!has_distinct_views(image))
        return gneiss::error{gneiss::error::internal};
      state.captured_dual = true;
    }
    return gneiss::error{gneiss::error::success};
  };
  example::scope_exit close{[&] {
    if (native_application && native_application->accepts_requests())
      (void)native_application->shutdown();
  }};
  auto create_result = gneiss::runtime::application::create(config);
  if (!create_result) {
    report_failure("创建 Application", create_result.error());
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
  gneiss::scene_instance scene{};
  example::scene_activation completion;
  // 启动阶段继续处理窗口事件和文件完成；不调用同步场景加载。
  while (!scene.id() || native_application->action_map_status() == gneiss::error::not_ready ||
         (completion.visuals && !completion.visuals.ready())) {
    (void)scheduler.run_ready();
    if (native_application->run(1).failed())
      return 2;
    if (!scene.id()) {
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
  example::report_visuals(completion);
  if (native_application->action_map_status().failed())
    return 2;
  example::scope_exit unload{[&] {
    if (scene.is_valid()) {
      (void)scene.unload();
    }
  }};
  const auto scene_ready = clock::now();
  auto result = native_application->action_map_status();
  if (result != gneiss::error::success) {
    report_failure("加载输入映射", result);
    return 4;
  }
  result = native_application->find_action("move_horizontal", state.orbit);
  if (result == gneiss::error::success) {
    result = native_application->find_action("quit", state.quit);
  }
  if (result != gneiss::error::success) {
    report_failure("查找输入动作", result);
    return 5;
  }
  if (!scene.is_valid())
    return 2;
  if (gneiss::get_active_camera(*native_application->native_world(), state.camera_node).failed()) {
    report_failure("场景需要一个主相机", gneiss::error::not_found);
    return 6;
  }
  const auto setup_ready = clock::now();

  can_update = true;
  state.previous_update = clock::now();
  std::uint64_t frame_count = smoke ? 3U : 0U;
  if (measure) {
    frame_count = measure_warmup_frames + measure_sample_frames;
  }
  // 冒烟模式更换一组不同宽高比目标，验证同一场景的双视图与目标释放。
  const unsigned rounds = dual_view && smoke ? 2U : 1U;
  for (unsigned round = 0; round < rounds; ++round) {
    std::array<gneiss::rid, 2> targets{};
    if (dual_view) {
      for (std::size_t i = 0; i < targets.size(); ++i) {
        auto target = native_application->render().create_target(
            {.width = round == 0 ? 320U : 512U, .height = i == 0 ? 240U : 384U});
        if (!target)
          return 7;
        targets[i] = *target;
      }
      state.targets = targets;
      state.captured_dual = false;
    }
    result = native_application->run(frame_count);
    if (result.failed()) {
      report_failure("运行主循环", result);
      return 7;
    }
    if (dual_view) {
      if (smoke && !state.captured_dual) {
        report_failure("双视图回读未完成", gneiss::error::not_ready);
        return 7;
      }
      result = native_application->render().finish_frames();
      if (result.failed())
        return 7;
      state.targets = {};
      if (!native_application->set_views({}))
        return 7;
      for (auto target : targets) {
        auto released = native_application->render().destroy_target(target);
        if (!released) {
          report_failure("释放双视图目标", released.error());
          return 7;
        }
      }
    }
  }
  const auto run_finished = clock::now();
  result = ([&] {
    auto status = scene.unload();
    if (status == gneiss::error::success)
      scene = {};
    return gneiss::error{status};
  })();
  if (result != gneiss::error::success) {
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
      report_failure("采集稳定帧", gneiss::error::internal);
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
  gneiss::install_fatal_handler();
  try {
    bool smoke = false;
    bool measure = false;
    bool dual_view = false;
    std::string assets;
    std::string scene = "asset://scenes/temple.scene.json";
    std::string input = "asset://input/default.input-map.json";
    for (int i = 1; i < argc; ++i) {
      const std::string_view option{argv[i]};
      if (option == "--smoke")
        smoke = true;
      else if (option == "--dual-view")
        dual_view = true;
      else if (option == "--measure")
        measure = true;
      else if ((option == "--assets" || option == "--scene" || option == "--input") &&
               i + 1 < argc) {
        auto& destination = option == "--assets" ? assets : option == "--scene" ? scene : input;
        destination = argv[++i];
      } else {
        std::fprintf(
            stderr,
            "用法：gneiss_scene_viewer [--smoke|--measure] [--dual-view] [--assets 路径] [--scene "
            "URI] [--input URI]\n");
        return option == "--help" ? 0 : 64;
      }
    }
    if (smoke && measure)
      return 64;
    return run_sample(argv[0], smoke, measure, dual_view, assets, scene, input);
  } catch (...) {
    gneiss::fatal_current_exception();
  }
}
