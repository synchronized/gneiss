// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/logging.hpp"
#include "../../support/value_result.hpp"
#include <array>
#include <chrono>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/asset/render_resource.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/scene.hpp>

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
  config.callbacks.update = [&](const gneiss::frame_time& time) {
    check(time.delta_ns == 0U);
    ++updates;
    return gneiss::error::success;
  };
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::error::success);
  bool allocation_threw = false;
  try {
    (void)app->step();
  } catch (const std::bad_alloc&) {
    allocation_threw = true;
  }
  check(allocation_threw);
  fail = false;
  check(app->step().ok() && updates == 1U);
  app.reset();
  config.callbacks.now_ns = {};
  config.callbacks.update = [](const gneiss::frame_time&) -> gneiss::error {
    throw std::runtime_error("更新失败");
  };
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::error::success);
  bool update_threw = false;
  try {
    (void)app->step();
  } catch (const std::runtime_error&) {
    update_threw = true;
  }
  check(update_threw);
  check(app->shutdown() == gneiss::error::success);
  app.reset();
  config.callbacks.update = {};
  config.callbacks.poll_events = [](bool& close) {
    close = true;
    return gneiss::error::success;
  };
  config.callbacks.close_requested = []() -> bool { throw std::bad_alloc{}; };
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::error::success);
  allocation_threw = false;
  try {
    (void)app->step();
  } catch (const std::bad_alloc&) {
    allocation_threw = true;
  }
  check(allocation_threw);
  check(app->shutdown() == gneiss::error::success);
  app.reset();
  unsigned delivered_diagnostics{};
  bool valid_diagnostic{};
  config = {};
  config.callbacks.initialize = [] { return gneiss::error::initialization_failed; };
  config.callbacks.diagnostic = [&](const gneiss::diagnostic_view& event) {
    ++delivered_diagnostics;
    valid_diagnostic =
        event.result == gneiss::error::initialization_failed && !event.message.empty();
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
        gneiss::error::success);
  gneiss::asset_batch_request empty_assets;
  check(!empty_assets.progress() && empty_assets.cancel() == gneiss::error::invalid_state);
  const auto rejected = [&] {
    check(app->step().error() == gneiss::error::invalid_state);
    auto assets = app->load_assets({});
    return !assets && assets.error() == gneiss::error::invalid_state;
  };
  bool rejected_on_worker{};
  std::thread worker([&] { rejected_on_worker = rejected(); });
  worker.join();
  check(rejected_on_worker);
  check(app->shutdown() == gneiss::error::success);
  check(rejected());
}

void test_loading_request_lifetime() {
  using namespace gneiss;
  gneiss::tasks::task_scheduler tasks(
      {.workers = 1, .mode = gneiss::tasks::execution_mode::cooperative});
  runtime::application_configuration config;
  config.asset_root = ".";
  config.executor = &tasks;
  auto created = runtime::application::create(config);
  check(bool(created));
  auto app = std::move(*created);
  check(app->can_load_assets());
  const gneiss::render_resource_source source{"asset://missing.texture",
                                              gneiss::render_resource_kind::texture};
  auto submitted = app->load_assets({&source, 1});
  check(bool(submitted));
  auto request = std::move(*submitted);
  check(bool(request) && !bool(*submitted) && !request.ready());
  check(!app->can_load_assets());
  bool worker_rejected{};
  std::thread worker([&] {
    worker_rejected = !request.progress() && request.cancel() == gneiss::error::invalid_state;
  });
  worker.join();
  check(worker_rejected && bool(request));
  // 放弃批次释放操作槽位；已移动请求的清理不能取消新所有者。
  submitted->reset();
  check(bool(request));
  request.reset();
  check(app->can_load_assets());
  submitted = app->load_assets({&source, 1});
  check(bool(submitted));
  request = std::move(*submitted);
  check(request.cancel().ok() && request.ready());
  check(!app->can_load_assets());
  auto cancelled = request.take();
  check(app->can_load_assets());
  check(bool(cancelled) && cancelled->state == gneiss::asset_load_state::cancelled && !request);
  // 每个 Application 的批次独立；关闭一个不会使另一个的请求失效。
  runtime::application_configuration other_config;
  other_config.executor = &tasks;
  auto other_created = runtime::application::create(other_config);
  check(bool(other_created));
  auto independent = (*other_created)->load_assets({});
  check(bool(independent) && independent->ready());
  submitted = app->load_assets({&source, 1});
  check(bool(submitted));
  request = std::move(*submitted);
  check(app->shutdown().ok() && !app->can_load_assets());
  check(!request && bool(*independent));
  app.reset();
  auto expired = request.take();
  check(!expired && expired.error() == gneiss::error::invalid_state);
  check(bool(independent->take()) && !bool(*independent));
  request.reset();
}

void asynchronous_shutdown(gneiss::tasks::execution_mode mode) {
  for (int stage = 0; stage < 3; ++stage) {
    gneiss::tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
    gneiss::runtime::application_configuration config;
    config.asset_root = GNEISS_TEST_ASSET_ROOT;
    config.executor = &scheduler;
    unsigned closed{};
    config.callbacks.shutdown = [&] { ++closed; };
    std::shared_ptr<gneiss::runtime::application> app;
    check(
        gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app).ok());
    gneiss::asset_request<gneiss::scene> request;
    gneiss::asset_batch_request assets_request;
    if (stage == 1) {
      check(gneiss::test_support::take_value(
                app->assets().load<gneiss::scene>("asset://scenes/triangle.scene.json"), request)
                .ok());
      (void)app->step();
    } else if (stage == 2) {
      const std::array sources{gneiss::render_resource_source{"asset://models/triangle.mesh.json",
                                                              gneiss::render_resource_kind::mesh}};
      check(gneiss::test_support::take_value(app->load_assets(sources, true), assets_request).ok());
      gneiss::asset_load_completion completion;
      bool ready{};
      check(app->step().ok());
      check(gneiss::test_support::receive_request(assets_request, completion, ready).ok());
    }
    app->request_exit();
    auto stopping = app->step();
    check(stopping && *stopping == gneiss::app::lifecycle::stopping);
    check(!app->accepts_requests() && closed == 0);
    app->request_exit();
    auto rejected = app->load_assets({});
    check(!rejected && rejected.error() == gneiss::error::invalid_state);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      const auto status = app->step();
      check(status.ok());
      if (*status == gneiss::app::lifecycle::stopped)
        break;
      check(*status == gneiss::app::lifecycle::stopping);
      check(std::chrono::steady_clock::now() < deadline);
      if (mode == gneiss::tasks::execution_mode::cooperative)
        (void)scheduler.run_ready();
      std::this_thread::yield();
    }
    check(closed == 1);
    check(!assets_request);
    if (request)
      check(request.status() != gneiss::error::not_ready);
    check(scheduler.stats().retained == 0 && scheduler.stats().running == 0);
    auto stopped = app->step();
    check(stopped && *stopped == gneiss::app::lifecycle::stopped && closed == 1);
    app.reset();
    check(closed == 1);
  }
}

void test_platform_callbacks() {
  std::shared_ptr<gneiss::runtime::application> app;
  // 预期初始化失败使用错误码并执行配对关闭；意外异常向调用边界传播。
  gneiss::runtime::application_configuration platform_config;
  unsigned shutdowns{};
  platform_config.callbacks.initialize = []() -> gneiss::error {
    return gneiss::error::initialization_failed;
  };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(platform_config),
                                           app) == gneiss::error::initialization_failed);
  check(!app && shutdowns == 1U);
  platform_config.callbacks.initialize = [] { return gneiss::error::success; };
  unsigned polls{};
  platform_config.callbacks.poll_events = [&](bool& close) {
    ++polls;
    close = true;
    return gneiss::error::success;
  };
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(platform_config),
                                           app) == gneiss::error::success);
  check(app->run().ok() && polls == 1U);
  app.reset();
  check(shutdowns == 2U);
  platform_config.callbacks.poll_events = [](bool&) -> gneiss::error { throw std::bad_alloc{}; };
  platform_config.callbacks.shutdown = [&] { ++shutdowns; };
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(platform_config),
                                           app) == gneiss::error::success);
  bool poll_threw = false;
  try {
    (void)app->step();
  } catch (const std::bad_alloc&) {
    poll_threw = true;
  }
  check(poll_threw);
  check(app->shutdown() == gneiss::error::success);
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
    return state.fail ? gneiss::error::initialization_failed : gneiss::error::success;
  };
  config.callbacks.shutdown = [&state] { ++state.shutdowns; };
  config.callbacks.update = [&state](const gneiss::frame_time&) -> gneiss::error {
    ++state.frames;
    return gneiss::error::success;
  };
  std::atomic<unsigned> delivered{};
  std::atomic<bool> valid_log{true};
  gneiss::test_support::log_capture logs{[&](const gneiss::logging::record_view& event) {
    if (event.message.category == "native-test") {
      if (event.message.message != "原生日志" || event.sequence == 0U) {
        valid_log.store(false);
      }
      ++delivered;
      // 正常回调验证投递与关闭；异常终止由 application-log-fatal 子进程测试覆盖。
    }
  }};
  // 不经 C API 和句柄注册表，真实对象仍使用同一创建、主循环及析构实现。
  std::shared_ptr<gneiss::runtime::application> app;
  auto created = gneiss::runtime::application::create(config);
  check(created && *created);
  app = std::move(*created);
  check(!*created && app);
  for (unsigned index = 0; index < 2U; ++index) {
    gneiss::logging::logger{"native-test"}.info("原生日志");
  }
  auto invalid = config;
  invalid.environment_intensity = -1.0F;
  auto retained = app;
  auto rejected = gneiss::runtime::application::create(invalid);
  check(!rejected && rejected.error() == gneiss::error::invalid_argument);
  check(app == retained && state.initializations == 1U);
  invalid = config;
  invalid.environment_asset = "invalid-uri";
  rejected = gneiss::runtime::application::create(invalid);
  check(!rejected && rejected.error() == gneiss::error::invalid_argument);
  check(app == retained && state.initializations == 1U);
  retained.reset();
  for (unsigned frame = 0; frame < 3; ++frame)
    check(app->step().ok());
  check(state.frames == 3U);
  gneiss::error other_thread{};
  std::thread worker([&] { other_thread = app->step().error(); });
  worker.join();
  check(other_thread == gneiss::error::invalid_state && state.frames == 3U);
  check(state.shutdowns == 0U);
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app) ==
        gneiss::error::initialization_failed);
  check(app == previous && state.initializations == 2U && state.shutdowns == 1U);
  previous.reset();
  check(app->step().ok() && state.frames == 4U);
  check(app->shutdown() == gneiss::error::success);
  check(app->step().error() == gneiss::error::invalid_state);
  app.reset();
  check(state.shutdowns == 2U);
  logs.service.flush();
  check(delivered.load() == 2U && valid_log.load());
  // 释放最后一个拥有者触发关闭，关闭回调恰好一次。
  state.fail = false;
  check(gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app) ==
        gneiss::error::success);
  app.reset();
  check(state.shutdowns == 3U);
  test_asset_request_boundaries();
  test_loading_request_lifetime();
  asynchronous_shutdown(gneiss::tasks::execution_mode::thread_pool);
  asynchronous_shutdown(gneiss::tasks::execution_mode::cooperative);
  test_platform_callbacks();
  test_loop_callback_exceptions();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
