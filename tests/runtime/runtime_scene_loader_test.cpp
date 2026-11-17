// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include "scene/runtime_scene_loader.hpp"
#include <algorithm>
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
  auto bytes = gneiss::asset_file::serialize_bytes(description);
  check(bool(bytes));
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
  check(bool(stream));
}
void run() {
  fixture files;
  gneiss::tasks::task_scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
  runtime::application_configuration config;
  const auto root = files.root.generic_string();
  config.asset_root = root;
  auto created = runtime::application::create(config);
  check(bool(created));
  auto app = *created;
  check(app->attach_task_executor(scheduler).ok());
  host::runtime_scene_loader loader(*app);
  gneiss::ref<host::scene_reload_state> source;
  loader.after_activate = [&](const auto& completion) {
    source = completion.reload_state;
    return gneiss::error{gneiss::error::success};
  };
  const auto tick = [&] {
    (void)scheduler.run_ready();
    check(app->run(1).ok());
    check(loader.advance().ok());
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
    check(loader.reload(source, target, prefab) == gneiss::error::not_ready);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      tick();
      auto done = loader.take_reload();
      if (done) {
        drain();
        return done->result;
      }
      check(done.error() == gneiss::error::not_ready);
      check(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
  };
  const auto address =
      gneiss::object_address{source->content->description->objects.front().uuid, {}};
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
  gneiss::object_address prefab_address;
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
  // 对照来源 UUID 保留身份：新增、删除、换根均不要求运行对象保存 Prefab 状态。
  const auto find_source = [&](gneiss::guid id) {
    auto found = std::ranges::find_if(source->objects,
                                      [&](const auto& item) { return item.first.source == id; });
    check(found != source->objects.end());
    return found->second.entity;
  };
  const auto original_root = prefab.root_uuid;
  const auto root_entity = find_source(original_root);
  gneiss::game_object_description extra;
  extra.uuid = gneiss::guid::generate();
  extra.parent.object = original_root;
  prefab.objects.push_back(extra);
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  const auto extra_entity = find_source(extra.uuid);
  gneiss::math::transform moved;
  moved.translation.x = 7;
  check(app->native_world()->find(extra_entity).set_local_transform(moved).ok());
  prefab.objects.back().name = "Renamed extra";
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  check(find_source(extra.uuid) == extra_entity);
  check(app->native_world()->find(extra_entity).name() == "Renamed extra");
  check(app->native_world()->find(extra_entity).local_transform().translation.x == 7);

  // 删除新增的叶子，旧句柄必须失效；用另一个新对象验证换根时现存身份不变。
  prefab.objects.pop_back();
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  check(!app->native_world()->object(extra_entity));
  extra.uuid = gneiss::guid::generate();
  extra.parent = {};
  prefab.objects.push_back(extra);
  for (auto& object : prefab.objects)
    if (object.uuid == original_root)
      object.parent.object = extra.uuid;
  prefab.root_uuid = extra.uuid;
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  const auto replacement_root = find_source(extra.uuid);
  check(find_source(original_root) == root_entity);
  auto parent = app->native_world()->find(root_entity).parent();
  check(parent && parent.entity() == replacement_root);
  std::ofstream(files.root / prefab_uri.substr(8)) << "broken";
  check(reload(prefab_uri, true).failed());
  check(find_source(original_root) == root_entity);
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  check(reload("asset://missing.scene.json", false).failed());
  check(source->synchronized && source->objects.at(address).entity == entity);
  check(loader.reload({}, uri, false) == gneiss::error::invalid_state);
  entity_id camera;
  check(gneiss::get_active_camera(*app->native_world(), camera) == gneiss::error::not_ready);
  prefab.objects.back().components.set(gneiss::camera_description{{1.0F, 0.1F, 100.0F}, true});
  save(files.root / prefab_uri.substr(8), prefab);
  check(reload(prefab_uri, true).ok());
  check(loader.request({.session = 1, .revision = 2, .uri = uri}, 2).ok());
  drain();
  check(loader.last_result().ok());
  check(gneiss::get_active_camera(*app->native_world(), camera).ok());
  check(camera == find_source(extra.uuid));
  auto instance = gneiss::scene_instance::borrow(*app->native_world(), (source->instance).id());
  check(instance.has_value());
  instance->unload();
  check(loader.reload(source, uri, false).failed());
}
}
int main() try {
  check(gneiss::initialize_meta().ok());
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
