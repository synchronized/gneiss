// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application.hpp"

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
  // 失败创建不替换原对象；失败初始化的部分资源必须恰好清理一次。
  auto previous = app;
  state.fail = true;
  check(gneiss::engine::Application::create(config, app) == GNEISS_ERROR_INITIALIZATION_FAILED);
  check(app == previous && state.initializations == 2U && state.shutdowns == 1U);
  previous.reset();
  check(app->run(GNEISS_NULL_APPLICATION, 1U) == GNEISS_SUCCESS && state.frames == 4U);
  app.reset();
  check(state.shutdowns == 2U);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
