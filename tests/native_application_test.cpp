// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application.hpp"
#include "engine/function/application/application_registry.hpp"

#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition) {
  if (!condition) {
    throw std::runtime_error("原生 Application 生命周期验收失败");
  }
}
struct Context {
  std::uint64_t frames{}, shutdowns{}, initializations{};
  bool fail{};
};
}

int main() try {
  Context state;
  gneiss::application_internal::application_configuration config;
  config.callbacks.user_data = &state;
  config.callbacks.initialize = [](void* data) -> gneiss_result {
    auto& value = *static_cast<Context*>(data);
    ++value.initializations;
    return value.fail ? GNEISS_ERROR_INITIALIZATION_FAILED : GNEISS_SUCCESS;
  };
  config.callbacks.shutdown = [](void* data) { ++static_cast<Context*>(data)->shutdowns; };
  config.callbacks.update = [](gneiss_application handle, const gneiss_frame_time*,
                               void* data) -> gneiss_result {
    if (handle != GNEISS_NULL_APPLICATION) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    ++static_cast<Context*>(data)->frames;
    return GNEISS_SUCCESS;
  };
  // 不经 C API 和句柄注册表，真实对象仍使用同一创建、主循环及析构实现。
  std::shared_ptr<gneiss::engine::Application> app;
  check(gneiss::engine::Application::create(config, app) == GNEISS_SUCCESS && app != nullptr);
  check(app->run(GNEISS_NULL_APPLICATION, 3U) == GNEISS_SUCCESS && state.frames == 3U);
  gneiss_result other_thread{};
  std::thread worker([&] { other_thread = app->run(GNEISS_NULL_APPLICATION, 1U); });
  worker.join();
  check(other_thread == GNEISS_ERROR_INVALID_STATE && state.frames == 3U);
  // 桥接只借用：撤销、误销毁和错误线程均不能关闭原生对象。
  gneiss::application_internal::ApplicationBridge bridge;
  const auto references = app.use_count();
  check(bridge.bind(app) == GNEISS_SUCCESS && app.use_count() == references);
  const auto old_handle = bridge.get();
  gneiss_world world{};
  check(gneiss_application_get_world(old_handle, &world) == GNEISS_SUCCESS);
  check(gneiss_application_destroy(old_handle) == GNEISS_ERROR_INVALID_STATE);
  check(state.shutdowns == 0U);
  std::thread reset_worker([&] { other_thread = bridge.reset(); });
  reset_worker.join();
  check(other_thread == GNEISS_ERROR_INVALID_STATE && bridge.get() == old_handle);
  check(bridge.reset() == GNEISS_SUCCESS && bridge.reset() == GNEISS_SUCCESS);
  check(gneiss_application_get_world(old_handle, &world) == GNEISS_ERROR_INVALID_HANDLE);
  check(bridge.bind(app) == GNEISS_SUCCESS && bridge.get() != old_handle);
  gneiss::application_internal::ApplicationBridge moved(std::move(bridge));
  // NOLINTNEXTLINE(bugprone-use-after-move): 验证自定义移动构造保证源桥接为空。
  check(bridge.get() == GNEISS_NULL_APPLICATION);
  check(gneiss_application_get_world(moved.get(), &world) == GNEISS_SUCCESS);
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::engine::Application::create(config, app) == GNEISS_ERROR_INITIALIZATION_FAILED);
  check(app == previous && state.initializations == 2U && state.shutdowns == 1U);
  previous.reset();
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == GNEISS_SUCCESS && state.frames == 4U);
  check(app->shutdown(GNEISS_NULL_APPLICATION) == GNEISS_SUCCESS);
  check(gneiss_application_get_world(moved.get(), &world) == GNEISS_ERROR_INVALID_HANDLE);
  check(bridge.bind(app) == GNEISS_ERROR_INVALID_STATE);
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == GNEISS_ERROR_INVALID_STATE);
  app.reset();
  check(state.shutdowns == 2U);
  check(moved.reset() == GNEISS_SUCCESS);
  // 桥接不延长宿主寿命；宿主先析构，晚到的兼容访问仍必须失效。
  state.fail = false;
  check(gneiss::engine::Application::create(config, app) == GNEISS_SUCCESS);
  check(bridge.bind(app) == GNEISS_SUCCESS);
  const auto expired = bridge.get();
  app.reset();
  check(state.shutdowns == 3U);
  check(gneiss_application_get_world(expired, &world) == GNEISS_ERROR_INVALID_HANDLE);
  check(bridge.reset() == GNEISS_SUCCESS);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
