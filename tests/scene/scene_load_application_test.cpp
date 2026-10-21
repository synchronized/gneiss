// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/c_owner.hpp"
#include "engine/resource/loaders/mesh_binary.hpp"
#include "engine/resource/loaders/texture_ktx2.hpp"
#include "runtime/application_asset_reload_internal.hpp"
#include "runtime/application_scene_load_internal.hpp"

#include <gneiss/engine/application.h>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/log.hpp>
#include <gneiss/engine/render.hpp>
#include <gneiss/engine/scene.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>

namespace {
using namespace gneiss;
using namespace runtime;
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
    engine::resource::mesh_binary_data mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0}, {0, 0, 1}},
                     {{1, 0, 0}, {1, 0}, {0, 0, 1}},
                     {{0, 1, 0}, {0, 1}, {0, 0, 1}}};
    mesh.indices = {0, 1, 2};
    mesh.tangents.assign(3, {1, 0, 0, 1});
    mesh.uv1.assign(3, {0.5F, 0.5F});
    mesh.colors.assign(3, {1, 1, 1, 1});
    std::vector<std::byte> bytes;
    engine::resource::mesh_binary_diagnostic diagnostic;
    check(engine::resource::encode_mesh_binary(mesh, bytes, diagnostic) ==
          engine::resource::mesh_binary_result::success);
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
void cancel_drain(tasks::execution_mode mode) {
  fixture files;
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::result{gneiss_application_create(&desc, app.put())} == result::success);
  check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  constexpr std::string_view uri = "asset://scene.json";
  std::uint64_t request{};
  check(request_scene_load(app.get(), uri, 1U, 1U, request) == GNEISS_SUCCESS);
  scene_load_progress progress;
  bool available{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(gneiss::result{gneiss_application_run(app.get(), 1U)} == result::success);
    scene_load_completion interim;
    bool terminal{};
    check(poll_scene_load(app.get(), interim, terminal) == GNEISS_SUCCESS && !terminal);
    check(query_scene_load_progress(app.get(), progress, available) == GNEISS_SUCCESS && available);
  } while (!progress.gpu_in_flight);
  check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
  check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
  scene_load_completion completion;
  bool finished{};
  check(poll_scene_load(app.get(), completion, finished) == GNEISS_SUCCESS && finished);
  check(completion.progress.phase == scene_load_phase::cancelled &&
        completion.progress.cleanup_pending && !completion.progress.cleanup_complete);
  std::uint64_t rejected{};
  check(request_scene_load(app.get(), uri, 1U, 2U, rejected) == GNEISS_ERROR_NOT_READY);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    check(gneiss::result{gneiss_application_run(app.get(), 1U)} == result::success);
    check(poll_scene_load(app.get(), completion, finished) == GNEISS_SUCCESS && !finished);
    check(query_scene_load_progress(app.get(), progress, available) == GNEISS_SUCCESS && available);
  } while (!progress.cleanup_complete);
  check(!progress.cleanup_pending && progress.upload_reserved_bytes == 0U &&
        progress.application_logical_bytes == 0U && !progress.gpu_in_flight);
  check(request_scene_load(app.get(), uri, 1U, 2U, request) == GNEISS_SUCCESS);
  check(query_scene_load_progress(app.get(), progress, available) == GNEISS_SUCCESS && available &&
        progress.phase == scene_load_phase::preparing && !progress.cleanup_complete);
}

void run(tasks::execution_mode mode) {
  fixture files;
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::result{gneiss_application_create(&desc, app.put())} == result::success);
  // 未装配执行器时，宿主入口应保留空闲查询与未就绪操作的不同语义。
  std::uint64_t unattached_request = 99U;
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 1U, unattached_request) ==
            GNEISS_ERROR_NOT_READY &&
        unattached_request == 0U);
  scene_load_progress idle_progress;
  bool idle_active = true;
  check(query_scene_load_progress(app.get(), idle_progress, idle_active) == GNEISS_SUCCESS &&
        !idle_active);
  scene_load_completion idle_completion;
  bool idle_ready = true;
  check(poll_scene_load(app.get(), idle_completion, idle_ready) == GNEISS_ERROR_NOT_READY &&
        !idle_ready);
  check(cancel_scene_load(app.get(), 99U) == GNEISS_ERROR_NOT_READY);
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
  check(prepared.resident_bytes == 228U + sizeof(engine::render::material_resource));
  check(prepared.cpu_data_bytes == prepared.resident_bytes);
  check(prepared.application_logical_bytes >= prepared.resident_bytes);
  check(prepared.application_cpu_data_bytes >= prepared.cpu_data_bytes);
  check(prepared.available_bytes +
            std::max(prepared.application_logical_bytes, prepared.application_cpu_data_bytes) ==
        engine::render::render_resource_service::default_memory_limit);
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
  // 激活后的旧域由 session 延迟退役，期间不能接收下一次打开。
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_SUCCESS && count == 1U);
  check(request_scene_load(app.get(), uri, 1U, 2U, rejected) == GNEISS_ERROR_NOT_READY &&
        rejected == 0U);
  (void)drive();
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_ERROR_INVALID_HANDLE);

  // ready 阶段仍可取消，不改写活动域；迟到请求身份不能取消后续请求。
  check(request_scene_load(app.get(), uri, 1U, 2U, request) == GNEISS_SUCCESS);
  (void)await_phase(scene_load_phase::ready);
  check(cancel_scene_load(app.get(), first) == GNEISS_ERROR_NOT_READY);
  check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
  auto [finished, cancelled] = drive();
  check(finished && cancelled.progress.phase == scene_load_phase::cancelled);
  check(cancelled.progress.cleanup_complete && !cancelled.progress.gpu_in_flight);
  check(cancelled.progress.upload_reserved_bytes == 0U);
  check(cancelled.progress.application_logical_bytes == prepared.application_logical_bytes);
  check(cancelled.progress.application_cpu_data_bytes == prepared.application_cpu_data_bytes);
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
      check(result.second.progress.cleanup_complete &&
            result.second.progress.upload_reserved_bytes == 0U);
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
  // ready 的候选已准备默认版本选择，取消仍必须保留已经激活的 World。
  check(gneiss_application_get_world(app.get(), &observed) == GNEISS_SUCCESS);
  for (const auto phase :
       {scene_load_phase::preparing, scene_load_phase::instantiating, scene_load_phase::ready}) {
    check(request_scene_load(app.get(), uri, 1U, 5U, request) == GNEISS_SUCCESS);
    if (phase != scene_load_phase::preparing) {
      (void)await_phase(phase);
    }
    check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool stopped{};
    while (!stopped && std::chrono::steady_clock::now() < cancel_deadline) {
      auto [done, cancelled_result] = drive();
      if (done) {
        check(cancelled_result.progress.phase == scene_load_phase::cancelled);
        check(cancelled_result.progress.cleanup_complete &&
              cancelled_result.progress.upload_reserved_bytes == 0U);
        stopped = true;
      }
    }
    check(stopped && gneiss_world_entity_count(observed, &count) == GNEISS_SUCCESS && count == 1U);
  }
  // 无渲染依赖的空场景不需要采用批次，也必须正常激活。
  std::ofstream(files.root / "empty.scene")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000003","objects":[],"prefab_instances":[]})";
  check(request_scene_load(app.get(), "asset://empty.scene", 1U, 6U, request) == GNEISS_SUCCESS);
  (void)await_phase(scene_load_phase::ready);
  check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
  (void)drive();
  check(gneiss_application_get_world(app.get(), &observed) == GNEISS_SUCCESS);
  check(gneiss_world_entity_count(observed, &count) == GNEISS_SUCCESS && count == 0U);
  check(request_scene_load(app.get(), uri, 1U, 7U, request) == GNEISS_SUCCESS);
  app = gneiss::test_support::application_owner{};
  check(scheduler.stats().retained == 0U);
  check(gneiss_world_entity_count(observed, &count) == GNEISS_ERROR_INVALID_HANDLE);
}

void shared_scene_assets(tasks::execution_mode mode) {
  fixture files;
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::result{gneiss_application_create(&desc, app.put())} == result::success);
  check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  scene_load_completion completion;
  std::uint64_t request{};
  auto poll = [&] {
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    check(poll_scene_load(app.get(), completion, terminal) == GNEISS_SUCCESS);
    std::this_thread::yield();
    return terminal;
  };
  auto prepare = [&] {
    check(request_scene_load(app.get(), "asset://scene.json", 1U, 1U, request) == GNEISS_SUCCESS);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    scene_load_progress progress;
    do {
      check(std::chrono::steady_clock::now() < deadline && !poll());
      bool active{};
      check(query_scene_load_progress(app.get(), progress, active) == GNEISS_SUCCESS && active);
    } while (progress.phase != scene_load_phase::ready);
    return progress;
  };
  auto selected = [&](engine::render::render_asset_type type, const char* uri) {
    engine::render::render_asset_lease lease;
    check(query_ready_asset(app.get(), {.uri = uri, .type = type}, lease) == GNEISS_SUCCESS);
    return lease;
  };
  auto binding = [&] {
    constexpr std::string_view uuid = "00000000-0000-4000-8000-000000000002";
    gneiss_scene_node_id node{};
    check(gneiss_scene_instance_find_node(app.get(), completion.scene, uuid.data(), uuid.size(),
                                          &node) == GNEISS_SUCCESS);
    mesh_binding value;
    check(capture_mesh_binding(app.get(), completion.scene, node, value) == GNEISS_SUCCESS);
    return value;
  };
  const auto first = prepare();
  check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
  auto original = binding();
  auto mesh = selected(engine::render::render_asset_type::mesh, "asset://mesh.json");
  auto material = selected(engine::render::render_asset_type::material, "asset://material.json");
  gneiss_world old_world{};
  check(gneiss_application_get_world(app.get(), &old_world) == GNEISS_SUCCESS);
  check(!poll());
  // 两个 World 同时持有相同版本，不重复计量或创建后端资源。
  const auto second = prepare();
  check(second.application_logical_bytes == first.application_logical_bytes);
  check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
  auto reused = binding();
  check(reused.mesh == mesh && reused.material == material);
  gneiss_world new_world{};
  check(gneiss_application_get_world(app.get(), &new_world) == GNEISS_SUCCESS &&
        old_world != new_world);
  std::uint64_t count{};
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_SUCCESS);
  check(!poll());
  check(gneiss_world_entity_count(old_world, &count) == GNEISS_ERROR_INVALID_HANDLE);
  check(gneiss_world_entity_count(new_world, &count) == GNEISS_SUCCESS && count == 1U);
  // 新版本 ready 不改变默认选择；取消也不得改变活动实例的固定租约。
  std::ofstream(files.root / "material.json")
      << R"({"format":"gneiss.material","version":1,"color":[0,1,0,1]})";
  (void)prepare();
  check(selected(engine::render::render_asset_type::material, "asset://material.json") == material);
  check(cancel_scene_load(app.get(), request) == GNEISS_SUCCESS);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!poll()) {
    check(std::chrono::steady_clock::now() < deadline);
  }
  check(completion.progress.phase == scene_load_phase::cancelled &&
        completion.progress.cleanup_complete);
  check(selected(engine::render::render_asset_type::material, "asset://material.json") == material);
  // 重试成功后才采用 B；旧租约 A 与未变化的网格依旧有效。
  (void)prepare();
  check(activate_scene_load(app.get(), request, completion) == GNEISS_SUCCESS);
  auto changed = binding();
  check(changed.mesh == mesh && changed.material != material);
  check(selected(engine::render::render_asset_type::material, "asset://material.json") ==
        changed.material);
  check(!poll());
  check(original.mesh == mesh && original.material == material);
  // 损坏的新来源失败后，B 的默认选择与活动 World 保持不变。
  check(gneiss_application_get_world(app.get(), &new_world) == GNEISS_SUCCESS);
  std::ofstream(files.root / "material.json") << "broken";
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 2U, request) == GNEISS_SUCCESS);
  const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!poll()) {
    check(std::chrono::steady_clock::now() < failure_deadline);
  }
  check(completion.progress.phase == scene_load_phase::failed &&
        completion.progress.cleanup_complete);
  check(selected(engine::render::render_asset_type::material, "asset://material.json") ==
        changed.material);
  check(gneiss_world_entity_count(new_world, &count) == GNEISS_SUCCESS && count == 1U);
}

void shared_texture_versions(tasks::execution_mode mode) {
  fixture files;
  std::vector<std::byte> bytes;
  std::string diagnostic;
  check(engine::resource::encode_texture_ktx2(
            {
                .transfer = engine::resource::texture_transfer::srgb,
                .levels =
                    {
                        {
                            .width = 1U,
                            .height = 1U,
                            .pixels = std::vector<std::byte>(4U, std::byte{255}),
                        },
                    },
            },
            bytes, diagnostic) == engine::resource::texture_ktx2_result::success);
  {
    std::ofstream output(files.root / "image.ktx2", std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  std::ofstream(files.root / "texture.json")
      << R"({"format":"gneiss.texture","version":1,"source":"asset://image.ktx2","color_space":"srgb"})";
  for (const auto* name : {"material.json", "other.json"}) {
    std::ofstream(files.root / name)
        << R"({"format":"gneiss.material","version":2,"color":[1,1,1,1],"base_color_texture":"asset://texture.json"})";
  }
  std::ofstream(files.root / "scene.json")
      << R"({"format":"gneiss.scene","version":4,"scene_uuid":"00000000-0000-4000-8000-000000000001","objects":[)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000002","parent":null,"transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://material.json"}}},)"
      << R"({"uuid":"00000000-0000-4000-8000-000000000003","parent":null,"transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{"mesh_renderer":{"mesh":"asset://mesh.json","material":"asset://other.json"}}}],"prefab_instances":[]})";
  tasks::task_scheduler scheduler({.workers = 1U, .mode = mode});
  gneiss::test_support::application_owner app;
  auto desc = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  const auto root = files.root.string();
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  check(gneiss::result{gneiss_application_create(&desc, app.put())} == result::success);
  check(attach_task_executor(app.get(), scheduler) == GNEISS_SUCCESS);
  std::uint64_t request{};
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 1U, request) == GNEISS_SUCCESS);
  scene_load_completion scene_completion;
  scene_load_progress progress;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    bool active{};
    check(poll_scene_load(app.get(), scene_completion, terminal) == GNEISS_SUCCESS && !terminal);
    check(query_scene_load_progress(app.get(), progress, active) == GNEISS_SUCCESS && active);
    std::this_thread::yield();
  } while (progress.phase != scene_load_phase::ready);
  // 材质各自一批准备，共享纹理必须复用同一 RID，绑定检查及预算去重均应成功。
  check(progress.texture_payload_bytes == 4U);
  check(activate_scene_load(app.get(), request, scene_completion) == GNEISS_SUCCESS);
  std::uint64_t count{};
  check(gneiss_scene_instance_get_node_count(app.get(), scene_completion.scene, &count) ==
            GNEISS_SUCCESS &&
        count == 2U);
  const std::array<std::string, 1> textures{"asset://texture.json"};
  check(request_textures(app.get(), textures, 1U, 2U, request) == GNEISS_SUCCESS);
  engine::render::texture_load_completion assets;
  bool ready{};
  while (!ready) {
    check(std::chrono::steady_clock::now() < deadline);
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    check(poll_textures(app.get(), assets, ready) == GNEISS_SUCCESS);
    std::this_thread::yield();
  }
  // 激活后的原始 VFS 必须与候选包装转交同一来源身份，否则会重复创建并上传版本。
  check(assets.result == GNEISS_SUCCESS && assets.assets.size() == 3U &&
        assets.versions.size() == 3U && assets.peak_upload_bytes == 0U);
  // 第一批材质准备后修改共享纹理；后续批次不能与旧批次混合激活。
  bool ignored{};
  check(poll_scene_load(app.get(), scene_completion, ignored) == GNEISS_SUCCESS && !ignored);
  gneiss_world active_world{};
  check(gneiss_application_get_world(app.get(), &active_world) == GNEISS_SUCCESS);
  check(request_scene_load(app.get(), "asset://scene.json", 1U, 3U, request) == GNEISS_SUCCESS);
  bool changed = false;
  const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (true) {
    check(std::chrono::steady_clock::now() < failure_deadline);
    if (mode == tasks::execution_mode::cooperative) {
      (void)scheduler.run_ready();
    }
    bool terminal{};
    bool active{};
    check(poll_scene_load(app.get(), scene_completion, terminal) == GNEISS_SUCCESS);
    if (terminal) {
      check(changed && scene_completion.result == GNEISS_ERROR_INVALID_STATE &&
            scene_completion.progress.phase == scene_load_phase::failed &&
            scene_completion.progress.cleanup_complete);
      break;
    }
    check(query_scene_load_progress(app.get(), progress, active) == GNEISS_SUCCESS && active);
    if (!changed && progress.phase == scene_load_phase::assets &&
        progress.texture_payload_bytes != 0U && progress.completed < progress.total) {
      std::ofstream(files.root / "image.ktx2", std::ios::app).put('\0');
      changed = true;
    }
    std::this_thread::yield();
  }
  gneiss_world preserved{};
  check(gneiss_application_get_world(app.get(), &preserved) == GNEISS_SUCCESS &&
        preserved == active_world);
  check(gneiss_world_entity_count(preserved, &count) == GNEISS_SUCCESS && count == 2U);
}
} // namespace
int main() try {
  cancel_drain(tasks::execution_mode::thread_pool);
  cancel_drain(tasks::execution_mode::cooperative);
  run(tasks::execution_mode::thread_pool);
  run(tasks::execution_mode::cooperative);
  shared_scene_assets(tasks::execution_mode::thread_pool);
  shared_scene_assets(tasks::execution_mode::cooperative);
  shared_texture_versions(tasks::execution_mode::thread_pool);
  shared_texture_versions(tasks::execution_mode::cooperative);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
