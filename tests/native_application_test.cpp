// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"
#include "engine/function/render/texture_load_service.hpp"
#include "engine/function/scene/scene_instance_service.hpp"

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
  application_internal::application_configuration config;
  std::shared_ptr<engine::application> app;
  bool fail = true;
  unsigned updates{};
  config.callbacks.now_ns = [&]() -> std::uint64_t {
    if (fail)
      throw std::bad_alloc{};
    return 12U;
  };
  config.callbacks.update = [&](core::rid, const engine::frame_time& time) {
    check(time.delta_ns == 0U);
    ++updates;
    return result::success;
  };
  check(engine::application::create(config, app) == GNEISS_SUCCESS);
  check(app->run(0, 1) == GNEISS_ERROR_OUT_OF_MEMORY);
  fail = false;
  check(app->run(0, 1) == GNEISS_SUCCESS && updates == 1U);
  app.reset();
  config.callbacks.now_ns = {};
  config.callbacks.update = [](core::rid, const engine::frame_time&) -> result {
    throw std::runtime_error("更新失败");
  };
  check(engine::application::create(config, app) == GNEISS_SUCCESS);
  check(app->run(0, 1) == GNEISS_ERROR_INTERNAL);
  check(app->shutdown(0) == GNEISS_SUCCESS);
  app.reset();
  config.callbacks.update = {};
  config.callbacks.poll_events = [](bool& close) {
    close = true;
    return result::success;
  };
  config.callbacks.close_requested = [](core::rid) -> bool { throw std::bad_alloc{}; };
  check(engine::application::create(config, app) == GNEISS_SUCCESS);
  check(app->run(0, 1) == GNEISS_ERROR_OUT_OF_MEMORY);
  check(app->shutdown(0) == GNEISS_SUCCESS);
  app.reset();
  unsigned delivered_diagnostics{};
  bool valid_diagnostic{};
  config = {};
  config.callbacks.initialize = [] { return result::initialization_failed; };
  config.callbacks.diagnostic = [&](core::rid, const diagnostics::diagnostic_view& event) {
    ++delivered_diagnostics;
    valid_diagnostic = event.result == result::initialization_failed && !event.message.empty();
    throw std::runtime_error("诊断失败");
  };
  check(engine::application::create(config, app) == GNEISS_ERROR_INITIALIZATION_FAILED);
  check(delivered_diagnostics == 1U && valid_diagnostic && !app);
}

void test_asset_request_boundaries() {
  using namespace gneiss;
  application_internal::application_configuration config;
  std::shared_ptr<engine::application> app;
  check(engine::application::create(config, app) == GNEISS_SUCCESS);
  render_internal::asset_load_progress progress;
  check(!app->asset_progress(progress));
  check(app->cancel_assets() == GNEISS_ERROR_NOT_READY);
  const auto rejected = [&] {
    std::uint64_t request = 17;
    bool ready = true;
    application_internal::scene_load_completion scene_completion;
    scene_completion.scene = 29;
    application_internal::scene_load_progress scene_progress;
    check(app->request_scene("project://missing.scene", 1, 1, request) ==
              GNEISS_ERROR_INVALID_STATE &&
          request == 0);
    check(app->poll_scene(scene_completion, ready) == GNEISS_ERROR_INVALID_STATE && !ready);
    check(app->cancel_scene(1) == GNEISS_ERROR_INVALID_STATE);
    check(app->activate_scene(1, scene_completion) == GNEISS_ERROR_INVALID_STATE);
    check(!app->scene_progress(scene_progress) && scene_completion.scene == 29);
    render_internal::texture_load_completion completion;
    completion.request = 23;
    render_internal::render_asset_lease lease;
    scene_internal::scene_instance_service::mesh_binding binding;
    gneiss::scene_node_id created{19};
    check(app->create_mesh_node_ready(0, {}, {}, created) == GNEISS_ERROR_INVALID_STATE &&
          !created.is_valid());
    check(app->collect_unused_edit_assets() == GNEISS_ERROR_INVALID_STATE);
    check(app->query_ready_asset({}, lease) == GNEISS_ERROR_INVALID_STATE && !lease);
    check(app->capture_mesh_binding(0, {}, binding) == GNEISS_ERROR_INVALID_STATE);
    check(app->replace_mesh_binding(0, {}, binding, binding) == GNEISS_ERROR_INVALID_STATE);
    check(app->prepare_edit_assets({}, 1, request) == GNEISS_ERROR_INVALID_STATE && request == 0);
    bool accepted = false;
    check(app->accept_edit_assets(completion,
                                  [&] {
                                    accepted = true;
                                    return GNEISS_SUCCESS;
                                  }) == GNEISS_ERROR_INVALID_STATE &&
          !accepted);

    return app->request_assets({}, 1, 1, request, true) == GNEISS_ERROR_INVALID_STATE &&
           request == 0 && app->poll_assets(completion, ready) == GNEISS_ERROR_INVALID_STATE &&
           !ready && completion.request == 23 && !app->asset_progress(progress) &&
           app->cancel_assets() == GNEISS_ERROR_INVALID_STATE;
  };
  bool rejected_on_worker{};
  std::thread worker([&] { rejected_on_worker = rejected(); });
  worker.join();
  check(rejected_on_worker);
  check(app->shutdown(0) == GNEISS_SUCCESS);
  check(rejected());
}

void test_platform_callbacks() {
  std::shared_ptr<gneiss::engine::application> app;
  // 原生平台回调使用强类型结果和 bool；异常不能跳过配对关闭。
  gneiss::application_internal::application_configuration platform_config;
  unsigned shutdowns{};
  platform_config.callbacks.initialize = []() -> gneiss::result {
    throw std::runtime_error("初始化失败");
  };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::engine::application::create(platform_config, app) == GNEISS_ERROR_INTERNAL);
  check(!app && shutdowns == 1U);
  platform_config.callbacks.initialize = [] { return gneiss::result::success; };
  unsigned polls{};
  platform_config.callbacks.poll_events = [&](bool& close) {
    ++polls;
    close = true;
    return gneiss::result::success;
  };
  check(gneiss::engine::application::create(platform_config, app) == GNEISS_SUCCESS);
  check(app->run(GNEISS_NULL_APPLICATION, 3U) == GNEISS_SUCCESS && polls == 1U);
  app.reset();
  check(shutdowns == 2U);
  platform_config.callbacks.poll_events = [](bool&) -> gneiss::result { throw std::bad_alloc{}; };
  platform_config.callbacks.shutdown = [&] {
    ++shutdowns;
    throw std::runtime_error("关闭失败");
  };
  check(gneiss::engine::application::create(platform_config, app) == GNEISS_SUCCESS);
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == GNEISS_ERROR_OUT_OF_MEMORY);
  check(app->shutdown(GNEISS_NULL_APPLICATION) == GNEISS_ERROR_INTERNAL);
  check(!app->accepts_requests());
  app.reset();
  check(shutdowns == 3U);
}
} // namespace

int main() try {
  context state;
  gneiss::application_internal::application_configuration config;
  config.callbacks.initialize = [&state] {
    ++state.initializations;
    return state.fail ? gneiss::result::initialization_failed : gneiss::result::success;
  };
  config.callbacks.shutdown = [&state] { ++state.shutdowns; };
  config.callbacks.update = [&state](gneiss::core::rid handle,
                                     const gneiss::engine::frame_time&) -> gneiss::result {
    if (handle != GNEISS_NULL_APPLICATION) {
      return gneiss::result::invalid_handle;
    }
    ++state.frames;
    return gneiss::result::success;
  };
  std::atomic<unsigned> delivered{};
  std::atomic<bool> valid_log{true};
  config.callbacks.log = [&](const gneiss::log_internal::event_view& event) {
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
  std::shared_ptr<gneiss::engine::application> app;
  check(gneiss::engine::application::create(config, app) == GNEISS_SUCCESS && app != nullptr);
  for (unsigned index = 0; index < 2U; ++index) {
    check(app->submit_log({.category = "native-test", .message = "原生日志"}) == GNEISS_SUCCESS);
  }
  auto invalid = config;
  invalid.environment_intensity = -1.0F;
  auto retained = app;
  check(gneiss::engine::application::create(invalid, app) == GNEISS_ERROR_INVALID_ARGUMENT);
  check(app == retained && state.initializations == 1U);
  invalid = config;
  invalid.environment_asset = "invalid-uri";
  check(gneiss::engine::application::create(invalid, app) == GNEISS_ERROR_INVALID_ARGUMENT);
  check(app == retained && state.initializations == 1U);
  retained.reset();
  check(app->run(GNEISS_NULL_APPLICATION, 3U) == GNEISS_SUCCESS && state.frames == 3U);
  gneiss_result other_thread{};
  std::thread worker([&] { other_thread = app->run(GNEISS_NULL_APPLICATION, 1U); });
  worker.join();
  check(other_thread == GNEISS_ERROR_INVALID_STATE && state.frames == 3U);
  // 桥接只借用：撤销、误销毁和错误线程均不能关闭原生对象。
  gneiss::application_internal::application_bridge bridge;
  const auto references = app.use_count();
  check(bridge.bind(app) == GNEISS_SUCCESS && app.use_count() == references);
  const auto old_handle = bridge.get();
  gneiss_world world{};
  check(gneiss_application_get_world(old_handle, &world) == GNEISS_SUCCESS);
  check(gneiss_application_destroy(old_handle) == GNEISS_ERROR_INVALID_STATE);
  const auto borrowed_world = world;
  check(gneiss_world_destroy(borrowed_world) == GNEISS_ERROR_INVALID_STATE);
  gneiss_entity_id owned_entity{};
  check(gneiss_world_entity_create(borrowed_world, &owned_entity) == GNEISS_SUCCESS);
  check(state.shutdowns == 0U);
  std::thread reset_worker([&] { other_thread = bridge.reset(); });
  reset_worker.join();
  check(other_thread == GNEISS_ERROR_INVALID_STATE && bridge.get() == old_handle);
  check(bridge.reset() == GNEISS_SUCCESS && bridge.reset() == GNEISS_SUCCESS);
  check(gneiss_application_get_world(old_handle, &world) == GNEISS_ERROR_INVALID_HANDLE);
  check(bridge.bind(app) == GNEISS_SUCCESS && bridge.get() != old_handle);
  gneiss::application_internal::application_bridge moved(std::move(bridge));
  // NOLINTNEXTLINE(bugprone-use-after-move): 验证自定义移动构造保证源桥接为空。
  check(bridge.get() == GNEISS_NULL_APPLICATION);
  check(gneiss_application_get_world(moved.get(), &world) == GNEISS_SUCCESS);
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::engine::application::create(config, app) == GNEISS_ERROR_INITIALIZATION_FAILED);
  check(app == previous && state.initializations == 2U && state.shutdowns == 1U);
  previous.reset();
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == GNEISS_SUCCESS && state.frames == 4U);
  check(app->shutdown(GNEISS_NULL_APPLICATION) == GNEISS_SUCCESS);
  check(gneiss_application_get_world(moved.get(), &world) == GNEISS_ERROR_INVALID_HANDLE);
  check(bridge.bind(app) == GNEISS_ERROR_INVALID_STATE);
  std::uint64_t count{};
  check(gneiss_world_entity_count(borrowed_world, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(gneiss_world_destroy(borrowed_world) == GNEISS_ERROR_INVALID_HANDLE);
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == GNEISS_ERROR_INVALID_STATE);
  app.reset();
  check(state.shutdowns == 2U);
  check(delivered.load() == 2U && valid_log.load());
  check(moved.reset() == GNEISS_SUCCESS);
  // 桥接不延长宿主寿命；宿主先析构，晚到的兼容访问仍必须失效。
  state.fail = false;
  check(gneiss::engine::application::create(config, app) == GNEISS_SUCCESS);
  check(bridge.bind(app) == GNEISS_SUCCESS);
  const auto expired = bridge.get();
  app.reset();
  check(state.shutdowns == 3U);
  check(gneiss_application_get_world(expired, &world) == GNEISS_ERROR_INVALID_HANDLE);
  check(bridge.reset() == GNEISS_SUCCESS);
  test_asset_request_boundaries();
  test_platform_callbacks();
  test_loop_callback_exceptions();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
