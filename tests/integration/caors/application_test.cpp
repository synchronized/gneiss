// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/application/application.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
namespace {
void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
void run(const char* assets) {
  using namespace gneiss;
#ifdef GNEISS_TEST_CAORS_DISABLED
  (void)assets;
  std::shared_ptr<engine::application> app;
  check(engine::application::create({}, app) == GNEISS_SUCCESS, "create disabled");
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_ERROR_UNSUPPORTED &&
            app->action_map_status() == GNEISS_ERROR_UNSUPPORTED &&
            app->cancel_action_map() == GNEISS_ERROR_UNSUPPORTED,
        "disabled is explicit");
#else
  using namespace std::chrono_literals;
  tasks::task_scheduler scheduler({.workers = 1});
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-caors-application-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "input");
  const auto file = root / "input/default.input-map.json";
  std::filesystem::copy_file(std::filesystem::path(assets) / "input/default.input-map.json", file);
  struct cleanup {
    std::filesystem::path path;
    ~cleanup() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } guard{root};
  const auto root_text = root.generic_string();
  application_internal::application_configuration config;
  config.asset_root = root_text;
  std::shared_ptr<engine::application> app;
  unsigned frames = 0;
  bool seen_ready = false;
  config.callbacks.update = [&](auto, const auto&) {
    ++frames;
    if (app->action_map_status() == GNEISS_SUCCESS) {
      input_internal::action_id id;
      check(app->find_action("move_horizontal", id) == GNEISS_SUCCESS, "ready before update");
      seen_ready = true;
    }
    return result::success;
  };
  check(engine::application::create(config, app) == GNEISS_SUCCESS, "create");
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_ERROR_NOT_READY,
        "requires executor");
  check(app->attach_task_executor(scheduler) == GNEISS_SUCCESS, "attach");
  const auto wait = [&] {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    do {
      check(app->run(0, 1) == GNEISS_SUCCESS, "run");
      check(std::chrono::steady_clock::now() < deadline, "timeout");
      std::this_thread::sleep_for(1ms);
    } while (app->action_map_status() == GNEISS_ERROR_NOT_READY);
  };
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_SUCCESS, "begin");
  check(app->action_map_status() == GNEISS_ERROR_NOT_READY, "admission only");
  wait();
  check(app->action_map_status() == GNEISS_SUCCESS && frames > 0 && seen_ready, "applied");
  input_internal::action_id original;
  check(app->find_action("move_horizontal", original) == GNEISS_SUCCESS, "original action");
  {
    std::ofstream invalid(file);
    invalid << "{}";
  }
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_SUCCESS, "reload");
  wait();
  check(app->action_map_status() != GNEISS_SUCCESS, "invalid reload failed");
  input_internal::action_id retained;
  check(app->find_action("move_horizontal", retained) == GNEISS_SUCCESS && retained == original,
        "failure preserves action identity");
  check(app->begin_action_map("asset://input/missing.json") == GNEISS_SUCCESS, "cancel begin");
  check(app->cancel_action_map() == GNEISS_SUCCESS, "cancel");
  wait();
  check(app->action_map_status() == GNEISS_ERROR_INVALID_STATE, "cancel stays cancelled");
  std::filesystem::copy_file(std::filesystem::path(assets) / "input/default.input-map.json", file,
                             std::filesystem::copy_options::overwrite_existing);
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_SUCCESS,
        "begin before sync");
  check(app->load_action_map("asset://input/default.input-map.json") == GNEISS_SUCCESS,
        "sync unchanged");
  check(app->run(0, 2) == GNEISS_SUCCESS && app->action_map_status() == GNEISS_ERROR_INVALID_STATE,
        "sync cancels older async apply");
  gneiss_result cross_thread = GNEISS_SUCCESS;
  std::thread other(
      [&] { cross_thread = app->begin_action_map("asset://input/default.input-map.json"); });
  other.join();
  check(cross_thread == GNEISS_ERROR_INVALID_STATE, "owner thread enforced");
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_SUCCESS,
        "shutdown begin");
  check(app->shutdown(0) == GNEISS_SUCCESS, "shutdown in flight");
  check(app->begin_action_map("asset://input/default.input-map.json") == GNEISS_ERROR_INVALID_STATE,
        "shutdown rejects requests");
#endif
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2)
      return 2;
    run(argv[1]);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
