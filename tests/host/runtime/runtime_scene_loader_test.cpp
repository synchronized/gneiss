// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "application/application.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/resource/asset_file.hpp"
#include "runtime_scene_loader.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <thread>
using namespace gneiss;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("宿主重载失败，行=" + std::to_string(where.line()));
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-host-reload-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::copy(GNEISS_RUNTIME_TEST_ASSET_ROOT, root,
                          std::filesystem::copy_options::recursive);
  }
  ~fixture() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};
template <class T> void save(const std::filesystem::path& path, const T& description) {
  auto bytes = engine::asset_file::serialize_bytes(description);
  check(bool(bytes));
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
  check(bool(stream));
}
void run() {
  fixture files;
  engine::task_scheduler scheduler({.mode = engine::execution_mode::cooperative});
  runtime::application_configuration config;
  const auto root = files.root.generic_string();
  config.asset_root = root;
  auto created = runtime::application::create(config);
  check(bool(created));
  auto app = *created;
  check(app->attach_task_executor(scheduler).ok());
  host::runtime_scene_loader loader(*app);
  engine::ref<host::scene_reload_state> source;
  loader.after_activate = [&](const auto& completion) {
    source = completion.reload_state;
    return engine::error{engine::error::success};
  };
  const auto tick = [&] {
    (void)scheduler.run_ready();
    check(app->run(1).ok());
    check(loader.advance().ok());
    check(!app->scene_loading());
  };
  const auto drain = [&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (loader.busy() || loader.visual_loading()) {
      check(std::chrono::steady_clock::now() < deadline);
      tick();
      std::this_thread::yield();
    }
  };
  constexpr auto uri = "asset://scenes/prefab.scene.json";
  check(loader.request({.session = 1, .revision = 1, .uri = uri}, 1).ok());
  drain();
  check(source && loader.last_result().ok());
  const auto reload = [&](std::string_view target, bool prefab) {
    check(loader.reload(source, target, prefab).ok());
    check(loader.reload(source, target, prefab) == engine::error::not_ready);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      tick();
      auto done = loader.take_reload();
      if (done) {
        drain();
        return done->result;
      }
      check(done.error() == engine::error::not_ready);
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  const auto address =
      engine::object_address{source->content->description->objects.front().uuid, {}};
  const auto entity = source->objects.at(address).entity;
  auto description = *source->content->description;
  description.objects.front().name = "Updated scene";
  save(files.root / "scenes/prefab.scene.json", description);
  check(reload(uri, false).ok());
  check(source->objects.at(address).entity == entity);
  check(app->native_world()->object(entity)->name() == "Updated scene");
  const auto& dependency = *source->content->prefabs.begin();
  const auto prefab_uri = dependency.first;
  auto prefab = *dependency.second.description;
  // 实例根名称由场景覆盖，验证未覆盖的子对象名称更新。
  const auto source_id = prefab.objects.at(1).uuid;
  engine::object_address prefab_address;
  entity_id prefab_entity;
  for (const auto& [candidate, binding] : source->objects) {
    if (candidate.source == source_id) {
      prefab_address = candidate;
      prefab_entity = binding.entity;
      break;
    }
  }
  check(bool(prefab_entity));
  prefab.objects.at(1).name = "Updated prefab";
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  check(source->objects.at(prefab_address).entity == prefab_entity);
  check(app->native_world()->object(prefab_entity)->name() == "Updated prefab");
  check(reload("asset://missing.scene.json", false).failed());
  check(source->synchronized && source->objects.at(address).entity == entity);
  check(loader.reload({}, uri, false) == engine::error::invalid_state);
  auto instance = engine::scene_instance::borrow(*app->native_world(), source->instance);
  check(instance && instance->unload().ok());
  check(loader.reload(source, uri, false).failed());
}
}
int main() try {
  check(engine::initialize_meta().ok());
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
