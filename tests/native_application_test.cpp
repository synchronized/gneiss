// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/resource/render_resource.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

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
  config.callbacks.update = [&](const gneiss::engine::frame_time& time) {
    check(time.delta_ns == 0U);
    ++updates;
    return gneiss::engine::error::success;
  };
  check(runtime::application::create(config, app) == gneiss::engine::error::success);
  check(app->run(1) == gneiss::engine::error::out_of_memory);
  fail = false;
  check(app->run(1) == gneiss::engine::error::success && updates == 1U);
  app.reset();
  config.callbacks.now_ns = {};
  config.callbacks.update = [](const gneiss::engine::frame_time&) -> gneiss::engine::error {
    throw std::runtime_error("更新失败");
  };
  check(runtime::application::create(config, app) == gneiss::engine::error::success);
  check(app->run(1) == gneiss::engine::error::internal);
  check(app->shutdown() == gneiss::engine::error::success);
  app.reset();
  config.callbacks.update = {};
  config.callbacks.poll_events = [](bool& close) {
    close = true;
    return gneiss::engine::error::success;
  };
  config.callbacks.close_requested = []() -> bool { throw std::bad_alloc{}; };
  check(runtime::application::create(config, app) == gneiss::engine::error::success);
  check(app->run(1) == gneiss::engine::error::out_of_memory);
  check(app->shutdown() == gneiss::engine::error::success);
  app.reset();
  unsigned delivered_diagnostics{};
  bool valid_diagnostic{};
  config = {};
  config.callbacks.initialize = [] { return gneiss::engine::error::initialization_failed; };
  config.callbacks.diagnostic = [&](const gneiss::engine::diagnostic_view& event) {
    ++delivered_diagnostics;
    valid_diagnostic =
        event.result == gneiss::engine::error::initialization_failed && !event.message.empty();
    throw std::runtime_error("诊断失败");
  };
  check(runtime::application::create(config, app) == gneiss::engine::error::initialization_failed);
  check(delivered_diagnostics == 1U && valid_diagnostic && !app);
}

void test_asset_request_boundaries() {
  using namespace gneiss;
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  check(runtime::application::create(config, app) == gneiss::engine::error::success);
  gneiss::runtime::asset_load_progress progress;
  check(!app->asset_progress(progress));
  check(app->cancel_assets() == gneiss::engine::error::not_ready);
  const auto rejected = [&] {
    std::uint64_t request = 17;
    bool ready = true;
    runtime::scene_load_completion scene_completion;
    scene_completion.scene = gneiss::scene_instance_id{29};
    runtime::scene_load_progress scene_progress;
    check(app->request_scene("project://missing.scene", 1, 1, request) ==
              gneiss::engine::error::invalid_state &&
          request == 0);
    check(app->poll_scene(scene_completion, ready) == gneiss::engine::error::invalid_state &&
          !ready);
    check(app->cancel_scene(1) == gneiss::engine::error::invalid_state);
    check(app->activate_scene(1, scene_completion) == gneiss::engine::error::invalid_state);
    check(!app->scene_progress(scene_progress) &&
          scene_completion.scene == gneiss::scene_instance_id{29});
    gneiss::runtime::asset_load_completion completion;
    completion.request = 23;
    gneiss::engine::render_resource_ref lease;
    gneiss::engine::scene_mesh_binding binding;
    auto created = app->create_mesh_node_ready({}, {}, {});
    check(!created && created.error() == gneiss::engine::error::invalid_state);
    check(app->collect_unused_edit_assets() == gneiss::engine::error::invalid_state);
    check(app->query_ready_asset({}, lease) == gneiss::engine::error::invalid_state && !lease);
    auto captured = app->capture_mesh_binding({}, {});
    check(!captured && captured.error() == gneiss::engine::error::invalid_state);
    check(app->replace_mesh_binding({}, {}, binding, binding) ==
          gneiss::engine::error::invalid_state);
    check(app->prepare_edit_assets({}, 1, request) == gneiss::engine::error::invalid_state &&
          request == 0);
    return app->request_assets({}, 1, 1, request, true) == gneiss::engine::error::invalid_state &&
           request == 0 &&
           app->poll_assets(completion, ready) == gneiss::engine::error::invalid_state && !ready &&
           completion.request == 23 && !app->asset_progress(progress) &&
           app->cancel_assets() == gneiss::engine::error::invalid_state;
  };
  bool rejected_on_worker{};
  std::thread worker([&] { rejected_on_worker = rejected(); });
  worker.join();
  check(rejected_on_worker);
  check(app->shutdown() == gneiss::engine::error::success);
  check(rejected());
}

void test_platform_callbacks() {
  std::shared_ptr<gneiss::runtime::application> app;
  // 原生平台回调使用强类型结果和 bool；异常不能跳过配对关闭。
  gneiss::runtime::application_configuration platform_config;
  unsigned shutdowns{};
  platform_config.callbacks.initialize = []() -> gneiss::engine::error {
    throw std::runtime_error("初始化失败");
  };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::runtime::application::create(platform_config, app) ==
        gneiss::engine::error::internal);
  check(!app && shutdowns == 1U);
  platform_config.callbacks.initialize = [] { return gneiss::engine::error::success; };
  unsigned polls{};
  platform_config.callbacks.poll_events = [&](bool& close) {
    ++polls;
    close = true;
    return gneiss::engine::error::success;
  };
  check(gneiss::runtime::application::create(platform_config, app) ==
        gneiss::engine::error::success);
  check(app->run(3U) == gneiss::engine::error::success && polls == 1U);
  app.reset();
  check(shutdowns == 2U);
  platform_config.callbacks.poll_events = [](bool&) -> gneiss::engine::error {
    throw std::bad_alloc{};
  };
  platform_config.callbacks.shutdown = [&] {
    ++shutdowns;
    throw std::runtime_error("关闭失败");
  };
  check(gneiss::runtime::application::create(platform_config, app) ==
        gneiss::engine::error::success);
  check(app->run(1U) == gneiss::engine::error::out_of_memory);
  check(app->shutdown() == gneiss::engine::error::internal);
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
    return state.fail ? gneiss::engine::error::initialization_failed
                      : gneiss::engine::error::success;
  };
  config.callbacks.shutdown = [&state] { ++state.shutdowns; };
  config.callbacks.update = [&state](const gneiss::engine::frame_time&) -> gneiss::engine::error {
    ++state.frames;
    return gneiss::engine::error::success;
  };
  std::atomic<unsigned> delivered{};
  std::atomic<bool> valid_log{true};
  config.callbacks.log = [&](const gneiss::engine::event_view& event) {
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
  check(gneiss::runtime::application::create(config, app) == gneiss::engine::error::success &&
        app != nullptr);
  for (unsigned index = 0; index < 2U; ++index) {
    check(app->submit_log({.category = "native-test", .message = "原生日志"}) ==
          gneiss::engine::error::success);
  }
  auto invalid = config;
  invalid.environment_intensity = -1.0F;
  auto retained = app;
  check(gneiss::runtime::application::create(invalid, app) ==
        gneiss::engine::error::invalid_argument);
  check(app == retained && state.initializations == 1U);
  invalid = config;
  invalid.environment_asset = "invalid-uri";
  check(gneiss::runtime::application::create(invalid, app) ==
        gneiss::engine::error::invalid_argument);
  check(app == retained && state.initializations == 1U);
  retained.reset();
  check(app->run(3U) == gneiss::engine::error::success && state.frames == 3U);
  gneiss::engine::error other_thread{};
  std::thread worker([&] { other_thread = app->run(1U); });
  worker.join();
  check(other_thread == gneiss::engine::error::invalid_state && state.frames == 3U);
  // Application 持有的 World 不授予调用方销毁权。
  const auto borrowed_world = app->native_world();
  check(borrowed_world != nullptr);
  gneiss::entity_id owned_entity;
  check(app->native_world()->entity_create(owned_entity) == gneiss::engine::error::success);
  check(state.shutdowns == 0U);
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::runtime::application::create(config, app) ==
        gneiss::engine::error::initialization_failed);
  check(app == previous && state.initializations == 2U && state.shutdowns == 1U);
  previous.reset();
  check(app->run(1U) == gneiss::engine::error::success && state.frames == 4U);
  check(app->shutdown() == gneiss::engine::error::success);
  // 关闭后借用失效，不再解引用旧指针。
  check(app->native_world() == nullptr);
  check(app->run(1U) == gneiss::engine::error::invalid_state);
  app.reset();
  check(state.shutdowns == 2U);
  check(delivered.load() == 2U && valid_log.load());
  // 释放最后一个拥有者触发关闭，关闭回调恰好一次。
  state.fail = false;
  check(gneiss::runtime::application::create(config, app) == gneiss::engine::error::success);
  app.reset();
  check(state.shutdowns == 3U);
  test_asset_request_boundaries();
  test_platform_callbacks();
  test_loop_callback_exceptions();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
