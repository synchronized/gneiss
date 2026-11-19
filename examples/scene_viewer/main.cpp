// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scene_activation.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/input/service.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/rendering/render_camera.hpp>
#include <gneiss/engine/function/scene/rendering/world_render_binding.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/platform/window/service.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>

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
  gneiss::world* world{};
  gneiss::entity_id camera_node;
  gneiss::input::action_id orbit;
  gneiss::input::action_id quit;
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
  gneiss::input::action_state orbit{};
  gneiss::input::action_state quit{};
  const auto orbit_result = application.input().get_action_state(state->orbit, orbit);
  if (orbit_result.failed()) {
    return orbit_result;
  }
  const auto quit_result = application.input().get_action_state(state->quit, quit);
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
  return state->world->find(gneiss::entity_id{(state->camera_node).get()})
      .set_local_transform(transform);
}

// 两个视图共享宿主 World，UI 引用宿主持有的离屏目标。
gneiss::result<void> configure_dual_view(gneiss::runtime::application& application,
                                         const std::array<gneiss::rid, 2>& targets,
                                         gneiss::ui_draw_list& ui) {
  using namespace gneiss;
  const auto surface = application.window()->surface();
  const float w = static_cast<float>(surface.width), h = static_cast<float>(surface.height);
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
  auto drawn = ui.replace({.display_width = w,
                           .display_height = h,
                           .vertices = vertices,
                           .indices = indices,
                           .commands = commands},
                          application.render());
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
  enum class mode { loading, viewing };
  gneiss::asset_request<gneiss::scene> scene_request;
  gneiss::asset_request<gneiss::input::action_map> input_request;
  gneiss::scene_instance scene{};
  example::scene_activation completion;
  auto scene_ready = started, setup_ready = started, run_finished = started,
       scene_unloaded = started;
  unsigned round{};
  std::uint64_t rendered_updates{};
  const auto frame_limit = measure ? measure_warmup_frames + measure_sample_frames
                           : smoke ? 3U
                                   : 0U;
  gneiss::runtime::application_configuration config;
  config.enable_window = true;
  config.window.title = title;
  config.asset_root = asset_root;
  config.executor = &scheduler;
  auto created = gneiss::runtime::application::create(config);
  if (!created) {
    report_failure("创建 Application", created.error());
    return 1;
  }
  auto world_result = gneiss::create_native();
  if (!world_result)
    return 1;
  auto world_owner = std::move(*world_result);
  state.world = world_owner.get();
  auto binding_result = gneiss::world_render_binding::create(*state.world, (*created)->render());
  if (!binding_result)
    return 1;
  auto binding = std::move(*binding_result);
  gneiss::ui_draw_list ui;
  const auto application_ready = clock::now();
  auto* native_application = created->get();
  auto release_targets = [&]() -> gneiss::result<void> {
    if (!state.targets[0] && !state.targets[1])
      return {};
    auto finished = native_application->render().finish_frames();
    if (finished.failed())
      return finished;
    ui.clear();
    for (auto& target : state.targets) {
      if (!target)
        continue;
      auto released = native_application->render().destroy_target(target);
      if (!released)
        return released;
      target = {};
    }
    return {};
  };
  auto create_targets = [&]() -> gneiss::result<void> {
    if (!dual_view)
      return {};
    for (std::size_t i = 0; i < state.targets.size(); ++i) {
      auto target = native_application->render().create_target(
          {.width = round == 0 ? 320U : 512U, .height = i == 0 ? 240U : 384U});
      if (!target)
        return target.error();
      state.targets[i] = *target;
    }
    state.captured_dual = false;
    return {};
  };
  // 清理函数及其捕获先于应用所有者建立，异常展开时仍可调用。
  auto* app = native_application;
  auto owner = std::move(*created);
  app->add_system(gneiss::app::stage::completions, "tasks", [&] { (void)scheduler.run_ready(); });
  app->add_system(gneiss::app::stage::synchronize, "scene.synchronize", [&] {
    state.world->flush_changes();
    return binding->synchronize();
  });
  app->add_system(gneiss::app::stage::present, "scene.present", [&]() -> gneiss::result<void> {
    using namespace gneiss;
    const auto surface = app->window()->surface();
    if (!surface.width || !surface.height)
      return {};
    render::frame_request frame;
    frame.surface = surface;
    const bool dual = bool(state.targets[0]) && bool(state.targets[1]);
    for (std::size_t i = 0; i < (dual ? 2U : 1U); ++i) {
      auto width = surface.width, height = surface.height;
      const auto target = dual ? state.targets[i] : rid{};
      if (target) {
        auto description = app->render().describe_target(target);
        if (!description)
          return description.error();
        width = description->width;
        height = description->height;
      }
      view_camera right;
      right.transform.translation = {0.0F, 3.0F, 9.0F};
      const float pitch = std::atan2(-3.0F, 9.0F) * 0.5F;
      right.transform.rotation = math::quaternion{std::cos(pitch), std::sin(pitch), 0, 0};
      auto camera = select_render_camera(*state.world, width, height, i == 1 ? &right : nullptr);
      if (!camera)
        return camera.error();
      if (*camera) {
        const auto& selected = **camera;
        frame.views.push_back({.scene = binding->scene(),
                               .target = target,
                               .view_matrix = selected.view,
                               .projection_matrix = selected.projection,
                               .camera_position = selected.transform.translation});
      } else if (!dual) {
        frame.views.push_back({});
      }
    }
    if (dual) {
      frame.views.push_back({});
      frame.ui = std::move(ui);
    }
    // 同步后回读本帧，验证两组尺寸和独立相机。
    if (dual && smoke && !state.captured_dual) {
      auto image = std::make_shared<render::frame_image>();
      auto submitted = app->render().request_capture(std::move(frame), image, {512, 256});
      if (!submitted)
        return submitted.error() == error::not_ready ? result<void>{} : submitted.error();
      auto finished = app->render().finish_frames();
      if (finished.failed())
        return finished;
      auto completed = app->render().try_take_required_completion();
      if (!completed)
        return completed.error();
      if (!*completed || (*completed)->sequence != *submitted)
        return error::internal;
      if ((*completed)->status.failed())
        return (*completed)->status;
      if (!has_distinct_views(*image))
        return error::internal;
      state.captured_dual = true;
      return {};
    }
    auto submitted = app->render().render(std::move(frame));
    return !submitted && submitted.error() != error::not_ready ? submitted : result<void>{};
  });
  app->init_state(mode::loading);
  app->on_enter(mode::loading, "load_scene", [&]() -> gneiss::result<void> {
    auto input = app->assets().reload<gneiss::input::action_map>(input_map_uri);
    if (!input)
      return input.error();
    input_request = std::move(*input);
    auto submitted = app->assets().load<gneiss::scene>(scene_uri);
    if (!submitted)
      return submitted.error();
    scene_request = std::move(*submitted);
    return {};
  });
  app->on_update(mode::loading, "await_scene", [&]() -> gneiss::result<void> {
    if (!scene.is_valid()) {
      const auto status = scene_request.status();
      if (status == gneiss::error::not_ready)
        return {};
      if (status.failed())
        return status;
      auto content = scene_request.snapshot();
      auto loaded = scene_request.take();
      if (!loaded || !content)
        return gneiss::error::invalid_state;
      auto activated = example::activate_scene(*app, *state.world, *content);
      if (!activated)
        return activated.error();
      completion = std::move(*activated);
      scene = completion.instance.instance;
    }
    const auto input = input_request.status();
    if (input == gneiss::error::not_ready || (completion.visuals && !completion.visuals.ready()))
      return {};
    if (input.failed())
      return input;
    auto loaded_input = input_request.take();
    if (!loaded_input)
      return loaded_input.error().error;
    const auto applied = app->input().replace_action_map(*(*loaded_input)->snapshot());
    if (applied.failed())
      return applied;
    example::report_visuals(completion);
    scene_ready = clock::now();
    return app->request_state(mode::viewing);
  });
  app->on_enter(mode::viewing, "configure_view", [&]() -> gneiss::result<void> {
    auto status = app->input().find_action("move_horizontal", state.orbit);
    if (status.failed())
      return status;
    status = app->input().find_action("quit", state.quit);
    if (status.failed())
      return status;
    status = gneiss::get_active_camera(*state.world, state.camera_node);
    if (status.failed())
      return status;
    auto targets = create_targets();
    if (!targets)
      return targets;
    setup_ready = state.previous_update = clock::now();
    return {};
  });
  app->on_update(mode::viewing, "view_scene",
                 [&](const gneiss::frame_time& time) -> gneiss::result<void> {
                   // 在下一帧开始切换目标，保证上一轮最后一帧已经提交。
                   if (frame_limit && rendered_updates == frame_limit) {
                     if (dual_view && smoke && !state.captured_dual)
                       return gneiss::error::not_ready;
                     if (dual_view && smoke && round == 0) {
                       auto released = release_targets();
                       if (!released)
                         return released;
                       ++round;
                       rendered_updates = 0;
                       auto targets = create_targets();
                       if (!targets)
                         return targets;
                     } else {
                       app->request_exit();
                       return {};
                     }
                   }
                   auto updated = update_sample(*app, time, &state);
                   if (updated.failed())
                     return updated;
                   ++rendered_updates;
                   if (!dual_view)
                     return {};
                   auto configured = configure_dual_view(*app, state.targets, ui);
                   if (!configured)
                     return configured;
                   return {};
                 });
  app->on_stop("release_targets", release_targets);
  app->on_stop("release_scene", [&] {
    run_finished = clock::now();
    scene_request = {};
    input_request.cancel();
    completion = {};
    if (scene.is_valid())
      scene.unload();
    scene = {};
    binding.reset();
    world_owner.reset();
    state.world = nullptr;
    scene_unloaded = clock::now();
  });
  auto result = app->run();
  if (!result) {
    report_failure("场景查看器生命周期", result.error());
    return 7;
  }
  owner.reset();
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
