// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_asset_reload_internal.hpp"
#include "engine/function/application/application_scene_load_internal.hpp"
#include "engine/asset/mesh_binary.hpp"

#include <gneiss/application.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace application_internal;
void check(bool condition, std::source_location where = std::source_location::current()) {
  if (!condition) {
    throw std::runtime_error("场景 Application 契约失败，行=" + std::to_string(where.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-scene-load-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    asset_internal::mesh_binary_data mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0}, {0, 0, 1}},
                     {{1, 0, 0}, {1, 0}, {0, 0, 1}},
                     {{0, 1, 0}, {0, 1}, {0, 0, 1}}};
    mesh.indices = {0, 1, 2};
    mesh.tangents.assign(3, {1, 0, 0, 1});
    mesh.uv1.assign(3, {0.5F, 0.5F});
    mesh.colors.assign(3, {1, 1, 1, 1});
    std::vector<std::byte> bytes;
    asset_internal::mesh_binary_diagnostic diagnostic;
    check(asset_internal::encode_mesh_binary(mesh, bytes, diagnostic) ==
          asset_internal::mesh_binary_result::success);
    std::ofstream output(root / "mesh.json", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    std::ofstream(root / "material.json")
        << R"({"format":"gneiss.material","version":1,"color":[1,0,0,1]})";
    std::ofstream(root / "scene.json")
        << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[{"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}}],"prefab_instances":[]})";
  }
  ~fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
void run(tasks::execution_mode mode) {
  fixture files;
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  application app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(application::create(desc, app) == result::success);
  check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  gneiss_world old_world{};
  check(gneiss_application_get_world(app.get(), &old_world) == GNEISS_SUCCESS);
  gneiss_entity_id old_entity{};
  check(gneiss_world_entity_create(old_world, &old_entity) == GNEISS_SUCCESS);
  constexpr std::string_view uri = "asset://scene.json";
  std::uint64_t request{};
  check(request_scene_load(app.get(), uri, 1U, 1U, request) == GNEISS_SUCCESS);
  const auto first = request;
  std::uint64_t rejected{};
  check(request_scene_load(app.get(), uri, 1U, 2U, rejected) == GNEISS_ERROR_NOT_READY &&
        rejected == 0U);
  auto drive = [&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    scene_load_completion completion;
    bool ready{};
    check(poll_scene_load(app.get(), completion, ready) == GNEISS_SUCCESS);
    std::this_thread::yield();
    return std::pair{ready, std::move(completion)};
  };
  auto await_phase = [&](scene_load_phase phase) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
      auto [finished, completion] = drive();
      check(!finished);
      scene_load_progress progress;
      bool active{};
      check(query_scene_load_progress(app.get(), progress, active) == GNEISS_SUCCESS && active);
      if (progress.phase == phase) {
        return progress;
      }
    }
    throw std::runtime_error("等待场景阶段超时");
  };
  const auto prepared = await_phase(scene_load_phase::ready);
  // v3 的三顶点共 216 B，索引 12 B；不得漏算切线、UV1、颜色的 120 B。
  check(prepared.resident_bytes == 228U + sizeof(render_internal::material_resource));
  check(prepared.cpu_data_bytes == prepared.resident_bytes);
  check(prepared.application_logical_bytes >= prepared.resident_bytes);
  check(prepared.application_cpu_data_bytes >= prepared.cpu_data_bytes);
  check(prepared.available_bytes +
            std::max(prepared.application_logical_bytes, prepared.application_cpu_data_bytes) ==
        render_internal::render_resource_service::default_memory_limit);
  check(prepared.upload_reserved_bytes == 0U);
  gneiss_world observed{};
  check(gneiss_application_get_world(app.get(), &observed) == GNEISS_SUCCESS &&
        observed == old_world);
  std::uint64_t count{};
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_SUCCESS && count == 1U);
  scene_load_completion completion;
  check(activate_scene_load(app.get(), first + 100U, completion) == GNEISS_ERROR_NOT_READY);
  check(activate_scene_load(app.get(), first, completion) == GNEISS_SUCCESS);
  check(completion.scene != GNEISS_NULL_SCENE_INSTANCE &&
        completion.progress.phase == scene_load_phase::applied);
  check(gneiss_application_get_world(app.get(), &observed) == GNEISS_SUCCESS &&
        observed != old_world);
  check(cancel_scene_load(app.get(), first) == GNEISS_ERROR_NOT_READY);
  (void)drive();
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_ERROR_INVALID_HANDLE);

  // ready 阶段仍可取消，不改写活动域；迟到请求身份不能取消后续请求。
  check(request_scene_load(app.get(), uri, 1U, 2U, request) == GNEISS_SUCCESS);
  (void)await_phase(scene_load_phase::ready);
  check(cancel_scene_load(app.get(), first) == GNEISS_ERROR_NOT_READY);
  check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
  auto [finished, cancelled] = drive();
  check(finished && cancelled.progress.phase == scene_load_phase::cancelled);
  check(gneiss_application_get_world(app.get(), &old_world) == GNEISS_SUCCESS &&
        old_world == observed);

  // 描述已准备后改变源文件：最终复读必须拒绝发布，旧 World 保持存活。
  check(request_scene_load(app.get(), uri, 1U, 3U, request) == GNEISS_SUCCESS);
  (void)await_phase(scene_load_phase::assets);
  std::ofstream(files.root / "scene.json", std::ios::app) << ' ';
  const auto changed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool changed{};
  while (!changed && std::chrono::steady_clock::now() < changed_deadline) {
    auto result = drive();
    if (result.first) {
      check(result.second.progress.phase == scene_load_phase::failed &&
            result.second.result == GNEISS_ERROR_INVALID_STATE);
      changed = true;
    }
  }
  check(changed && gneiss_world_entity_count(observed, &count) == GNEISS_SUCCESS && count == 1U);

  // 依赖损坏产生失败终态；随后修复可以重试，旧域保持存活。
  std::ofstream(files.root / "material.json") << "broken";
  check(request_scene_load(app.get(), uri, 1U, 3U, request) == GNEISS_SUCCESS);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool failed{};
  while (!failed && std::chrono::steady_clock::now() < deadline) {
    auto result = drive();
    if (result.first) {
      check(result.second.progress.phase == scene_load_phase::failed);
      failed = true;
    }
  }
  check(failed && gneiss_world_entity_count(observed, &count) == GNEISS_SUCCESS && count == 1U);
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  check(request_scene_load(app.get(), uri, 1U, 4U, request) == GNEISS_SUCCESS);
  (void)await_phase(scene_load_phase::ready);
  check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
  (void)drive();
  // 描述准备和实例化边界取消都必须保留已经激活的 World。
  check(gneiss_application_get_world(app.get(), &observed) == GNEISS_SUCCESS);
  for (const auto phase : {scene_load_phase::preparing, scene_load_phase::instantiating}) {
    check(request_scene_load(app.get(), uri, 1U, 5U, request) == GNEISS_SUCCESS);
    if (phase != scene_load_phase::preparing)
      (void)await_phase(phase);
    check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool stopped{};
    while (!stopped && std::chrono::steady_clock::now() < cancel_deadline) {
      auto [done, cancelled_result] = drive();
      if (done) {
        check(cancelled_result.progress.phase == scene_load_phase::cancelled);
        stopped = true;
      }
    }
    check(stopped && gneiss_world_entity_count(observed, &count) == GNEISS_SUCCESS && count == 1U);
  }
  check(request_scene_load(app.get(), uri, 1U, 5U, request) == GNEISS_SUCCESS);
  app = application{};
  check(scheduler.stats().retained == 0U);
}
} // namespace
int main() try {
  run(tasks::execution_mode::thread_pool);
  run(tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
