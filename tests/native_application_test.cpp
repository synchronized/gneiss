// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition) {
  if (!condition) {
    throw std::runtime_error("原生 Application 生命周期验收失败");
  }
}
struct context {
  std::uint64_t frames{}, shutdowns{}, initializations{};
  bool fail{};
};
}

namespace {
void test_loop_callback_exceptions() {
  using namespace gneiss;
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  bool fail = true;
  unsigned updates{};
  config.callbacks.now_ns = [&]() -> std::uint64_t {
    if (fail)
      throw std::bad_alloc{};
    return 12U;
  };
  config.callbacks.update = [&](engine::core::rid, const engine::core::frame_time& time) {
    check(time.delta_ns == 0U);
    ++updates;
    return gneiss::engine::core::result::success;
  };
  check(runtime::application::create(config, app) == gneiss::engine::core::result::success);
  check(app->run(0, 1) == gneiss::engine::core::result::out_of_memory);
  fail = false;
  check(app->run(0, 1) == gneiss::engine::core::result::success && updates == 1U);
  app.reset();
  config.callbacks.now_ns = {};
  config.callbacks.update = [](engine::core::rid,
                               const engine::core::frame_time&) -> gneiss::engine::core::result {
    throw std::runtime_error("更新失败");
  };
  check(runtime::application::create(config, app) == gneiss::engine::core::result::success);
  check(app->run(0, 1) == gneiss::engine::core::result::internal);
  check(app->shutdown(0) == gneiss::engine::core::result::success);
  app.reset();
  config.callbacks.update = {};
  config.callbacks.poll_events = [](bool& close) {
    close = true;
    return gneiss::engine::core::result::success;
  };
  config.callbacks.close_requested = [](engine::core::rid) -> bool { throw std::bad_alloc{}; };
  check(runtime::application::create(config, app) == gneiss::engine::core::result::success);
  check(app->run(0, 1) == gneiss::engine::core::result::out_of_memory);
  check(app->shutdown(0) == gneiss::engine::core::result::success);
  app.reset();
  unsigned delivered_diagnostics{};
  bool valid_diagnostic{};
  config = {};
  config.callbacks.initialize = [] { return gneiss::engine::core::result::initialization_failed; };
  config.callbacks.diagnostic = [&](engine::core::rid,
                                    const engine::core::diagnostics::diagnostic_view& event) {
    ++delivered_diagnostics;
    valid_diagnostic = event.result == gneiss::engine::core::result::initialization_failed &&
                       !event.message.empty();
    throw std::runtime_error("诊断失败");
  };
  check(runtime::application::create(config, app) ==
        gneiss::engine::core::result::initialization_failed);
  check(delivered_diagnostics == 1U && valid_diagnostic && !app);
}

void test_asset_request_boundaries() {
  using namespace gneiss;
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  check(runtime::application::create(config, app) == gneiss::engine::core::result::success);
  engine::render::asset_load_progress progress;
  check(!app->asset_progress(progress));
  check(app->cancel_assets() == gneiss::engine::core::result::not_ready);
  const auto rejected = [&] {
    std::uint64_t request = 17;
    bool ready = true;
    runtime::scene_load_completion scene_completion;
    scene_completion.scene = 29;
    runtime::scene_load_progress scene_progress;
    check(app->request_scene("project://missing.scene", 1, 1, request) ==
              gneiss::engine::core::result::invalid_state &&
          request == 0);
    check(app->poll_scene(scene_completion, ready) == gneiss::engine::core::result::invalid_state &&
          !ready);
    check(app->cancel_scene(1) == gneiss::engine::core::result::invalid_state);
    check(app->activate_scene(1, scene_completion) == gneiss::engine::core::result::invalid_state);
    check(!app->scene_progress(scene_progress) && scene_completion.scene == 29);
    engine::render::texture_load_completion completion;
    completion.request = 23;
    engine::render::render_asset_ref lease;
    engine::scene::scene_instances::mesh_binding binding;
    gneiss::scene_node_id created{19};
    check(app->create_mesh_node_ready(0, {}, {}, created) ==
              gneiss::engine::core::result::invalid_state &&
          !created.is_valid());
    check(app->collect_unused_edit_assets() == gneiss::engine::core::result::invalid_state);
    check(app->query_ready_asset({}, lease) == gneiss::engine::core::result::invalid_state &&
          !lease);
    check(app->capture_mesh_binding(0, {}, binding) == gneiss::engine::core::result::invalid_state);
    check(app->replace_mesh_binding(0, {}, binding, binding) ==
          gneiss::engine::core::result::invalid_state);
    check(app->prepare_edit_assets({}, 1, request) == gneiss::engine::core::result::invalid_state &&
          request == 0);
    return app->request_assets({}, 1, 1, request, true) ==
               gneiss::engine::core::result::invalid_state &&
           request == 0 &&
           app->poll_assets(completion, ready) == gneiss::engine::core::result::invalid_state &&
           !ready && completion.request == 23 && !app->asset_progress(progress) &&
           app->cancel_assets() == gneiss::engine::core::result::invalid_state;
  };
  bool rejected_on_worker{};
  std::thread worker([&] { rejected_on_worker = rejected(); });
  worker.join();
  check(rejected_on_worker);
  check(app->shutdown(0) == gneiss::engine::core::result::success);
  check(rejected());
}

void test_platform_callbacks() {
  std::shared_ptr<gneiss::runtime::application> app;
  // 原生平台回调使用强类型结果和 bool；异常不能跳过配对关闭。
  gneiss::runtime::application_configuration platform_config;
  unsigned shutdowns{};
  platform_config.callbacks.initialize = []() -> gneiss::engine::core::result {
    throw std::runtime_error("初始化失败");
  };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::runtime::application::create(platform_config, app) ==
        gneiss::engine::core::result::internal);
  check(!app && shutdowns == 1U);
  platform_config.callbacks.initialize = [] { return gneiss::engine::core::result::success; };
  unsigned polls{};
  platform_config.callbacks.poll_events = [&](bool& close) {
    ++polls;
    close = true;
    return gneiss::engine::core::result::success;
  };
  check(gneiss::runtime::application::create(platform_config, app) ==
        gneiss::engine::core::result::success);
  check(app->run(GNEISS_NULL_APPLICATION, 3U) == gneiss::engine::core::result::success &&
        polls == 1U);
  app.reset();
  check(shutdowns == 2U);
  platform_config.callbacks.poll_events = [](bool&) -> gneiss::engine::core::result {
    throw std::bad_alloc{};
  };
  platform_config.callbacks.shutdown = [&] {
    ++shutdowns;
    throw std::runtime_error("关闭失败");
  };
  check(gneiss::runtime::application::create(platform_config, app) ==
        gneiss::engine::core::result::success);
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == gneiss::engine::core::result::out_of_memory);
  check(app->shutdown(GNEISS_NULL_APPLICATION) == gneiss::engine::core::result::internal);
  check(!app->accepts_requests());
  app.reset();
  check(shutdowns == 3U);
}
} // namespace

int main() try {
  context state;
  gneiss::runtime::application_configuration config;
  config.callbacks.initialize = [&state] {
    ++state.initializations;
    return state.fail ? gneiss::engine::core::result::initialization_failed
                      : gneiss::engine::core::result::success;
  };
  config.callbacks.shutdown = [&state] { ++state.shutdowns; };
  config.callbacks.update =
      [&state](gneiss::engine::core::rid handle,
               const gneiss::engine::core::frame_time&) -> gneiss::engine::core::result {
    if (handle != GNEISS_NULL_APPLICATION) {
      return gneiss::engine::core::result::invalid_handle;
    }
    ++state.frames;
    return gneiss::engine::core::result::success;
  };
  std::atomic<unsigned> delivered{};
  std::atomic<bool> valid_log{true};
  config.callbacks.log = [&](const gneiss::engine::core::log::event_view& event) {
    if (event.message.category == "native-test") {
      if (event.message.message != "原生日志" || event.sequence == 0U) {
        valid_log.store(false);
      }
      ++delivered;
      // 投递器必须隔离用户回调异常，随后仍能投递和正常关闭。
      throw std::runtime_error("日志接收方失败");
    }
  };
  // 不经 C API 和句柄注册表，真实对象仍使用同一创建、主循环及析构实现。
  std::shared_ptr<gneiss::runtime::application> app;
  check(gneiss::runtime::application::create(config, app) ==
            gneiss::engine::core::result::success &&
        app != nullptr);
  for (unsigned index = 0; index < 2U; ++index) {
    check(app->submit_log({.category = "native-test", .message = "原生日志"}) ==
          gneiss::engine::core::result::success);
  }
  auto invalid = config;
  invalid.environment_intensity = -1.0F;
  auto retained = app;
  check(gneiss::runtime::application::create(invalid, app) ==
        gneiss::engine::core::result::invalid_argument);
  check(app == retained && state.initializations == 1U);
  invalid = config;
  invalid.environment_asset = "invalid-uri";
  check(gneiss::runtime::application::create(invalid, app) ==
        gneiss::engine::core::result::invalid_argument);
  check(app == retained && state.initializations == 1U);
  retained.reset();
  check(app->run(GNEISS_NULL_APPLICATION, 3U) == gneiss::engine::core::result::success &&
        state.frames == 3U);
  gneiss::engine::core::result other_thread{};
  std::thread worker([&] { other_thread = app->run(GNEISS_NULL_APPLICATION, 1U); });
  worker.join();
  check(other_thread == gneiss::engine::core::result::invalid_state && state.frames == 3U);
  // 桥接只借用：撤销、误销毁和错误线程均不能关闭原生对象。
  gneiss::runtime::application_bridge bridge;
  const auto references = app.use_count();
  check(bridge.bind(app) == gneiss::engine::core::result::success && app.use_count() == references);
  const auto old_handle = bridge.get();
  gneiss_world world{};
  check(gneiss::engine::core::result{gneiss_application_get_world(old_handle, &world)} ==
        gneiss::engine::core::result::success);
  check(gneiss::engine::core::result{gneiss_application_destroy(old_handle)} ==
        gneiss::engine::core::result::invalid_state);
  const auto borrowed_world = world;
  check(gneiss::engine::core::result{gneiss_world_destroy(borrowed_world)} ==
        gneiss::engine::core::result::invalid_state);
  gneiss_entity_id owned_entity{};
  check(gneiss::engine::core::result{gneiss_world_entity_create(borrowed_world, &owned_entity)} ==
        gneiss::engine::core::result::success);
  check(state.shutdowns == 0U);
  std::thread reset_worker([&] { other_thread = bridge.reset(); });
  reset_worker.join();
  check(other_thread == gneiss::engine::core::result::invalid_state && bridge.get() == old_handle);
  check(bridge.reset() == gneiss::engine::core::result::success &&
        bridge.reset() == gneiss::engine::core::result::success);
  check(gneiss::engine::core::result{gneiss_application_get_world(old_handle, &world)} ==
        gneiss::engine::core::result::invalid_handle);
  check(bridge.bind(app) == gneiss::engine::core::result::success && bridge.get() != old_handle);
  gneiss::runtime::application_bridge moved(std::move(bridge));
  // NOLINTNEXTLINE(bugprone-use-after-move): 验证自定义移动构造保证源桥接为空。
  check(bridge.get() == GNEISS_NULL_APPLICATION);
  check(gneiss::engine::core::result{gneiss_application_get_world(moved.get(), &world)} ==
        gneiss::engine::core::result::success);
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::runtime::application::create(config, app) ==
        gneiss::engine::core::result::initialization_failed);
  check(app == previous && state.initializations == 2U && state.shutdowns == 1U);
  previous.reset();
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == gneiss::engine::core::result::success &&
        state.frames == 4U);
  check(app->shutdown(GNEISS_NULL_APPLICATION) == gneiss::engine::core::result::success);
  check(gneiss::engine::core::result{gneiss_application_get_world(moved.get(), &world)} ==
        gneiss::engine::core::result::invalid_handle);
  check(bridge.bind(app) == gneiss::engine::core::result::invalid_state);
  std::uint64_t count{};
  check(gneiss::engine::core::result{gneiss_world_entity_count(borrowed_world, &count)} ==
        gneiss::engine::core::result::invalid_handle);
  check(gneiss::engine::core::result{gneiss_world_destroy(borrowed_world)} ==
        gneiss::engine::core::result::invalid_handle);
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == gneiss::engine::core::result::invalid_state);
  app.reset();
  check(state.shutdowns == 2U);
  check(delivered.load() == 2U && valid_log.load());
  check(moved.reset() == gneiss::engine::core::result::success);
  // 桥接不延长宿主寿命；宿主先析构，晚到的兼容访问仍必须失效。
  state.fail = false;
  check(gneiss::runtime::application::create(config, app) == gneiss::engine::core::result::success);
  check(bridge.bind(app) == gneiss::engine::core::result::success);
  const auto expired = bridge.get();
  app.reset();
  check(state.shutdowns == 3U);
  check(gneiss::engine::core::result{gneiss_application_get_world(expired, &world)} ==
        gneiss::engine::core::result::invalid_handle);
  check(bridge.reset() == gneiss::engine::core::result::success);
  test_asset_request_boundaries();
  test_platform_callbacks();
  test_loop_callback_exceptions();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
