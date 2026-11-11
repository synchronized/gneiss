// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application.hpp"
#include "engine/core/error.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/render_resource.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/scene.hpp"
#include "support/scene_access.hpp"
#include "support/value_result.hpp"
#include <array>
#include <chrono>

#include <atomic>
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, std::source_location where = std::source_location::current()) {
  if (!condition) {
    throw std::runtime_error("原生 Application 生命周期验收失败：" + std::to_string(where.line()));
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
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::engine::error::success);
  bool allocation_threw = false;
  try {
    (void)app->run(1);
  } catch (const std::bad_alloc&) {
    allocation_threw = true;
  }
  check(allocation_threw);
  fail = false;
  check(app->run(1) == gneiss::engine::error::success && updates == 1U);
  app.reset();
  config.callbacks.now_ns = {};
  config.callbacks.update = [](const gneiss::engine::frame_time&) -> gneiss::engine::error {
    throw std::runtime_error("更新失败");
  };
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::engine::error::success);
  bool update_threw = false;
  try {
    (void)app->run(1);
  } catch (const std::runtime_error&) {
    update_threw = true;
  }
  check(update_threw);
  check(app->shutdown() == gneiss::engine::error::success);
  app.reset();
  config.callbacks.update = {};
  config.callbacks.poll_events = [](bool& close) {
    close = true;
    return gneiss::engine::error::success;
  };
  config.callbacks.close_requested = []() -> bool { throw std::bad_alloc{}; };
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::engine::error::success);
  allocation_threw = false;
  try {
    (void)app->run(1);
  } catch (const std::bad_alloc&) {
    allocation_threw = true;
  }
  check(allocation_threw);
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
  bool diagnostic_threw = false;
  try {
    (void)runtime::application::create(config);
  } catch (const std::runtime_error&) {
    diagnostic_threw = true;
  }
  check(diagnostic_threw);
  check(delivered_diagnostics == 1U && valid_diagnostic && !app);
}

void test_asset_request_boundaries() {
  using namespace gneiss;
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::engine::error::success);
  engine::asset_batch_request empty_assets;
  check(!empty_assets.progress() && empty_assets.cancel() == engine::error::invalid_state);
  const auto rejected = [&] {
    check(app->run(1) == engine::error::invalid_state);
    auto assets = app->load_assets({});
    return !assets && assets.error() == engine::error::invalid_state;
  };
  bool rejected_on_worker{};
  std::thread worker([&] { rejected_on_worker = rejected(); });
  worker.join();
  check(rejected_on_worker);
  check(app->shutdown() == gneiss::engine::error::success);
  check(rejected());
}

void test_loading_request_lifetime() {
  using namespace gneiss;
  engine::task_scheduler tasks({.workers = 1, .mode = engine::execution_mode::cooperative});
  runtime::application_configuration config;
  config.asset_root = ".";
  auto created = runtime::application::create(config);
  check(bool(created));
  auto app = std::move(*created);
  check(app->attach_task_executor(tasks).ok());
  const engine::render_resource_source source{"asset://missing.texture",
                                              engine::render_resource_kind::texture};
  auto submitted = app->load_assets({&source, 1});
  check(bool(submitted));
  auto request = std::move(*submitted);
  check(bool(request) && !bool(*submitted) && !request.ready());
  bool worker_rejected{};
  std::thread worker([&] {
    worker_rejected = !request.progress() && request.cancel() == engine::error::invalid_state;
  });
  worker.join();
  check(worker_rejected && bool(request));
  // 放弃批次释放操作槽位；已移动请求的清理不能取消新所有者。
  submitted->reset();
  check(bool(request));
  request.reset();
  submitted = app->load_assets({&source, 1});
  check(bool(submitted));
  request = std::move(*submitted);
  check(request.cancel().ok() && request.ready());
  auto cancelled = request.take();
  check(bool(cancelled) && cancelled->state == engine::asset_load_state::cancelled && !request);
  // 每个 Application 的批次独立；关闭一个不会使另一个的请求失效。
  auto other_created = runtime::application::create({});
  check(bool(other_created));
  check((*other_created)->attach_task_executor(tasks).ok());
  auto independent = (*other_created)->load_assets({});
  check(bool(independent) && independent->ready());
  submitted = app->load_assets({&source, 1});
  check(bool(submitted));
  request = std::move(*submitted);
  check(app->shutdown().ok());
  check(!request && bool(*independent));
  app.reset();
  auto expired = request.take();
  check(!expired && expired.error() == engine::error::invalid_state);
  check(bool(independent->take()) && !bool(*independent));
  request.reset();
}

void asynchronous_shutdown(gneiss::engine::execution_mode mode) {
  for (int stage = 0; stage < 3; ++stage) {
    gneiss::engine::task_scheduler scheduler({.workers = 1U, .mode = mode});
    gneiss::runtime::application_configuration config;
    config.asset_root = GNEISS_TEST_ASSET_ROOT;
    unsigned closed{};
    config.callbacks.shutdown = [&] { ++closed; };
    std::shared_ptr<gneiss::runtime::application> app;
    check(
        gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app).ok());
    check(app->attach_task_executor(scheduler).ok());
    check(app->advance_shutdown() == gneiss::engine::error::invalid_state);
    gneiss::engine::asset_request<gneiss::engine::scene> request;
    gneiss::engine::asset_batch_request assets_request;
    if (stage == 1) {
      check(gneiss::test_support::take_value(
                app->assets().load<gneiss::engine::scene>("asset://scenes/triangle.scene.json"),
                request)
                .ok());
      (void)app->run(1);
    } else if (stage == 2) {
      const std::array sources{gneiss::engine::render_resource_source{
          "asset://models/triangle.mesh.json", gneiss::engine::render_resource_kind::mesh}};
      check(gneiss::test_support::take_value(app->load_assets(sources, true), assets_request).ok());
      gneiss::engine::asset_load_completion completion;
      bool ready{};
      check(app->run(1).ok());
      check(gneiss::test_support::receive_request(assets_request, completion, ready).ok());
    }
    check(app->request_shutdown().ok() && !app->accepts_requests() && closed == 0);
    check(app->request_shutdown().ok());
    auto rejected = app->load_assets({});
    check(!rejected && rejected.error() == gneiss::engine::error::invalid_state);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      const auto status = app->advance_shutdown();
      if (status != gneiss::engine::error::not_ready) {
        check(status.ok());
        break;
      }
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::engine::execution_mode::cooperative)
        (void)scheduler.run_ready();
      std::this_thread::yield();
    }
    check(app->native_world() == nullptr && closed == 1);
    check(!assets_request);
    if (request)
      check(request.status() != gneiss::engine::error::not_ready);
    check(scheduler.stats().retained == 0 && scheduler.stats().running == 0);
    check(app->advance_shutdown().ok() && closed == 1);
    app.reset();
    check(closed == 1);
  }
}

void test_platform_callbacks() {
  std::shared_ptr<gneiss::runtime::application> app;
  // 预期初始化失败使用错误码并执行配对关闭；意外异常向调用边界传播。
  gneiss::runtime::application_configuration platform_config;
  unsigned shutdowns{};
  platform_config.callbacks.initialize = []() -> gneiss::engine::error {
    return gneiss::engine::error::initialization_failed;
  };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(platform_config),
                                           app) == gneiss::engine::error::initialization_failed);
  check(!app && shutdowns == 1U);
  platform_config.callbacks.initialize = [] { return gneiss::engine::error::success; };
  unsigned polls{};
  platform_config.callbacks.poll_events = [&](bool& close) {
    ++polls;
    close = true;
    return gneiss::engine::error::success;
  };
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(platform_config),
                                           app) == gneiss::engine::error::success);
  check(app->run(3U) == gneiss::engine::error::success && polls == 1U);
  app.reset();
  check(shutdowns == 2U);
  platform_config.callbacks.poll_events = [](bool&) -> gneiss::engine::error {
    throw std::bad_alloc{};
  };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(platform_config),
                                           app) == gneiss::engine::error::success);
  bool poll_threw = false;
  try {
    (void)app->run(1U);
  } catch (const std::bad_alloc&) {
    poll_threw = true;
  }
  check(poll_threw);
  check(app->shutdown() == gneiss::engine::error::success);
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
      // 正常回调验证投递与关闭；异常终止由 application-log-fatal 子进程测试覆盖。
    }
  };
  // 不经 C API 和句柄注册表，真实对象仍使用同一创建、主循环及析构实现。
  std::shared_ptr<gneiss::runtime::application> app;
  auto created = gneiss::runtime::application::create(config);
  check(created && *created);
  app = std::move(*created);
  check(!*created && app);
  for (unsigned index = 0; index < 2U; ++index) {
    check(app->submit_log({.category = "native-test", .message = "原生日志"}) ==
          gneiss::engine::error::success);
  }
  auto invalid = config;
  invalid.environment_intensity = -1.0F;
  auto retained = app;
  auto rejected = gneiss::runtime::application::create(invalid);
  check(!rejected && rejected.error() == gneiss::engine::error::invalid_argument);
  check(app == retained && state.initializations == 1U);
  invalid = config;
  invalid.environment_asset = "invalid-uri";
  rejected = gneiss::runtime::application::create(invalid);
  check(!rejected && rejected.error() == gneiss::engine::error::invalid_argument);
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
  auto owned_scene = app->native_world()->create_scene();
  check(owned_scene.has_value());
  check(owned_scene->create_object().has_value());
  check(state.shutdowns == 0U);
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app) ==
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
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app) ==
        gneiss::engine::error::success);
  app.reset();
  check(state.shutdowns == 3U);
  test_asset_request_boundaries();
  test_loading_request_lifetime();
  asynchronous_shutdown(gneiss::engine::execution_mode::thread_pool);
  asynchronous_shutdown(gneiss::engine::execution_mode::cooperative);
  test_platform_callbacks();
  test_loop_callback_exceptions();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
