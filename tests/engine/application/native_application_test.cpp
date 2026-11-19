// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/logging.hpp"
#include "../../support/value_result.hpp"
#include <array>
#include <chrono>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
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
}

namespace {
void test_loop_callback_exceptions() {
  using namespace gneiss;
  for (bool startup : {true, false}) {
    bool acquired{};
    unsigned released{};
    auto created = runtime::application::create({});
    check(bool(created));
    auto app = std::move(*created);
    app->on_start("acquire", [&] {
      acquired = true;
      if (startup)
        throw std::runtime_error("启动失败");
    });
    app->add_system(app::stage::update, "throw", [] { throw std::runtime_error("更新失败"); });
    app->on_stop("release", [&] {
      check(acquired);
      acquired = false;
      ++released;
    });
    bool threw{};
    try {
      (void)app->step();
    } catch (const std::runtime_error&) {
      threw = true;
    }
    check(threw && acquired);
    app->request_exit();
    check(app->run().ok() && !acquired && released == 1);
    app.reset();
    check(released == 1);
  }
}

void test_asset_request_boundaries() {
  using namespace gneiss;
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::error::success);
  gneiss::asset_batch_request empty_assets;
  check(!empty_assets.progress() && empty_assets.cancel() == gneiss::error::invalid_state);
  bool rejected_on_worker{};
  std::thread worker([&] { rejected_on_worker = app->step().error() == error::invalid_state; });
  worker.join();
  check(rejected_on_worker);
  app->request_exit();
  check(app->run().ok());
  const auto stopped = app->step();
  check(stopped && *stopped == app::lifecycle::stopped);
  check(!app->accepts_requests());
}

void test_loading_request_lifetime() {
  using namespace gneiss;
  gneiss::tasks::scheduler tasks(
      {.workers = 1, .mode = gneiss::tasks::execution_mode::cooperative});
  runtime::application_configuration config;
  config.asset_root = ".";
  config.executor = &tasks;
  auto created = runtime::application::create(config);
  check(bool(created));
  auto app = std::move(*created);
  check(app->step().ok());
  const gneiss::render_resource_source source{"asset://missing.texture",
                                              gneiss::render_resource_kind::texture};
  auto submitted = app->assets().load_batch({&source, 1});
  check(bool(submitted));
  auto request = std::move(*submitted);
  check(bool(request) && !bool(*submitted) && !request.ready());
  bool worker_rejected{};
  std::thread worker([&] {
    worker_rejected = !request.progress() && request.cancel() == gneiss::error::invalid_state;
  });
  worker.join();
  check(worker_rejected && bool(request));
  // 放弃批次释放自身观察；已移动请求的清理不能取消新所有者。
  submitted->reset();
  check(bool(request));
  request.reset();
  submitted = app->assets().load_batch({&source, 1});
  check(bool(submitted));
  request = std::move(*submitted);
  check(request.cancel().ok() && request.ready());
  auto cancelled = request.take();
  check(bool(cancelled) && cancelled->state == gneiss::asset_load_state::cancelled && !request);
  // 每个 Application 的批次独立；关闭一个不会使另一个的请求失效。
  runtime::application_configuration other_config;
  other_config.executor = &tasks;
  auto other_created = runtime::application::create(other_config);
  check(bool(other_created));
  check((*other_created)->step().ok());
  auto independent = (*other_created)->assets().load_batch({});
  check(bool(independent) && independent->ready());
  submitted = app->assets().load_batch({&source, 1});
  check(bool(submitted));
  request = std::move(*submitted);
  app->request_exit();
  check(app->run().ok());
  check(!request && bool(*independent));
  app.reset();
  auto expired = request.take();
  check(!expired && expired.error() == gneiss::error::invalid_state);
  check(bool(independent->take()) && !bool(*independent));
  request.reset();
}

void asynchronous_shutdown(gneiss::tasks::execution_mode mode) {
  for (int stage = 0; stage < 3; ++stage) {
    gneiss::tasks::scheduler scheduler({.workers = 1U, .mode = mode});
    gneiss::runtime::application_configuration config;
    config.asset_root = GNEISS_TEST_ASSET_ROOT;
    config.executor = &scheduler;
    unsigned closed{};
    std::shared_ptr<gneiss::runtime::application> app;
    check(
        gneiss::test_support::assign_value(gneiss::runtime::application::create(config), app).ok());
    app->on_stop("closed", [&] { ++closed; });
    if (stage != 0)
      check(app->step().ok());
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
      check(
          gneiss::test_support::take_value(app->assets().load_batch(sources, true), assets_request)
              .ok());
      gneiss::asset_load_completion completion;
      bool ready{};
      check(app->step().ok());
      check(gneiss::test_support::receive_request(assets_request, completion, ready).ok());
    }
    app->request_exit();
    auto stopping = app->step();
    check(stopping && *stopping == gneiss::app::lifecycle::stopping);
    check(!app->accepts_requests() && closed == 1);
    app->request_exit();
    check(!assets_request);
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

void test_startup_failure() {
  using namespace gneiss;
  unsigned initialized{}, closed{}, updates{};
  auto created = runtime::application::create({});
  check(bool(created));
  auto app = std::move(*created);
  check(!app->async_files() && !app->window());
  app->on_start("fail", [&] {
    ++initialized;
    return error::initialization_failed;
  });
  app->add_system(app::stage::update, "never", [&] { ++updates; });
  app->on_stop("release", [&] { ++closed; });
  check(app->run().error() == error::initialization_failed);
  check(initialized == 1 && closed == 1 && updates == 0);
  check(app->step().error() == error::initialization_failed);
  app.reset();
  check(closed == 1);
}
} // namespace

int main() try {
  using namespace gneiss;
  unsigned starts{}, frames{}, stops{};
  auto created = runtime::application::create({});
  check(bool(created));
  auto app = std::move(*created);
  app->on_start("start", [&] { ++starts; });
  app->add_system(app::stage::update, "update", [&] { ++frames; });
  app->on_stop("stop", [&] { ++stops; });
  runtime::application_configuration invalid;
  invalid.enable_window = true;
  invalid.window.width = 0;
  check(runtime::application::create(invalid).error() == error::invalid_argument);
  check(starts == 0 && !app->async_files());
  for (unsigned frame = 0; frame < 3; ++frame)
    check(app->step().ok());
  check(starts == 1 && frames == 3);
  error other_thread;
  std::thread worker([&] { other_thread = app->step().error(); });
  worker.join();
  check(other_thread == error::invalid_state && frames == 3);
  app->request_exit();
  check(app->run().ok());
  check(*app->step() == app::lifecycle::stopped);
  app.reset();
  check(stops == 1);
  test_asset_request_boundaries();
  test_loading_request_lifetime();
  asynchronous_shutdown(tasks::execution_mode::thread_pool);
  asynchronous_shutdown(tasks::execution_mode::cooperative);
  test_startup_failure();
  test_loop_callback_exceptions();
  return 0;
} catch (...) {
  gneiss::fatal_current_exception();
}
